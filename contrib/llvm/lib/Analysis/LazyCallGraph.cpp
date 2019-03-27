//===- LazyCallGraph.cpp - Analysis of a Module's call graph --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <string>
#include <tuple>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "lcg"

void LazyCallGraph::EdgeSequence::insertEdgeInternal(Node &TargetN,
                                                     Edge::Kind EK) {
  EdgeIndexMap.insert({&TargetN, Edges.size()});
  Edges.emplace_back(TargetN, EK);
}

void LazyCallGraph::EdgeSequence::setEdgeKind(Node &TargetN, Edge::Kind EK) {
  Edges[EdgeIndexMap.find(&TargetN)->second].setKind(EK);
}

bool LazyCallGraph::EdgeSequence::removeEdgeInternal(Node &TargetN) {
  auto IndexMapI = EdgeIndexMap.find(&TargetN);
  if (IndexMapI == EdgeIndexMap.end())
    return false;

  Edges[IndexMapI->second] = Edge();
  EdgeIndexMap.erase(IndexMapI);
  return true;
}

static void addEdge(SmallVectorImpl<LazyCallGraph::Edge> &Edges,
                    DenseMap<LazyCallGraph::Node *, int> &EdgeIndexMap,
                    LazyCallGraph::Node &N, LazyCallGraph::Edge::Kind EK) {
  if (!EdgeIndexMap.insert({&N, Edges.size()}).second)
    return;

  LLVM_DEBUG(dbgs() << "    Added callable function: " << N.getName() << "\n");
  Edges.emplace_back(LazyCallGraph::Edge(N, EK));
}

LazyCallGraph::EdgeSequence &LazyCallGraph::Node::populateSlow() {
  assert(!Edges && "Must not have already populated the edges for this node!");

  LLVM_DEBUG(dbgs() << "  Adding functions called by '" << getName()
                    << "' to the graph.\n");

  Edges = EdgeSequence();

  SmallVector<Constant *, 16> Worklist;
  SmallPtrSet<Function *, 4> Callees;
  SmallPtrSet<Constant *, 16> Visited;

  // Find all the potential call graph edges in this function. We track both
  // actual call edges and indirect references to functions. The direct calls
  // are trivially added, but to accumulate the latter we walk the instructions
  // and add every operand which is a constant to the worklist to process
  // afterward.
  //
  // Note that we consider *any* function with a definition to be a viable
  // edge. Even if the function's definition is subject to replacement by
  // some other module (say, a weak definition) there may still be
  // optimizations which essentially speculate based on the definition and
  // a way to check that the specific definition is in fact the one being
  // used. For example, this could be done by moving the weak definition to
  // a strong (internal) definition and making the weak definition be an
  // alias. Then a test of the address of the weak function against the new
  // strong definition's address would be an effective way to determine the
  // safety of optimizing a direct call edge.
  for (BasicBlock &BB : *F)
    for (Instruction &I : BB) {
      if (auto CS = CallSite(&I))
        if (Function *Callee = CS.getCalledFunction())
          if (!Callee->isDeclaration())
            if (Callees.insert(Callee).second) {
              Visited.insert(Callee);
              addEdge(Edges->Edges, Edges->EdgeIndexMap, G->get(*Callee),
                      LazyCallGraph::Edge::Call);
            }

      for (Value *Op : I.operand_values())
        if (Constant *C = dyn_cast<Constant>(Op))
          if (Visited.insert(C).second)
            Worklist.push_back(C);
    }

  // We've collected all the constant (and thus potentially function or
  // function containing) operands to all of the instructions in the function.
  // Process them (recursively) collecting every function found.
  visitReferences(Worklist, Visited, [&](Function &F) {
    addEdge(Edges->Edges, Edges->EdgeIndexMap, G->get(F),
            LazyCallGraph::Edge::Ref);
  });

  // Add implicit reference edges to any defined libcall functions (if we
  // haven't found an explicit edge).
  for (auto *F : G->LibFunctions)
    if (!Visited.count(F))
      addEdge(Edges->Edges, Edges->EdgeIndexMap, G->get(*F),
              LazyCallGraph::Edge::Ref);

  return *Edges;
}

void LazyCallGraph::Node::replaceFunction(Function &NewF) {
  assert(F != &NewF && "Must not replace a function with itself!");
  F = &NewF;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void LazyCallGraph::Node::dump() const {
  dbgs() << *this << '\n';
}
#endif

static bool isKnownLibFunction(Function &F, TargetLibraryInfo &TLI) {
  LibFunc LF;

  // Either this is a normal library function or a "vectorizable" function.
  return TLI.getLibFunc(F, LF) || TLI.isFunctionVectorizable(F.getName());
}

LazyCallGraph::LazyCallGraph(Module &M, TargetLibraryInfo &TLI) {
  LLVM_DEBUG(dbgs() << "Building CG for module: " << M.getModuleIdentifier()
                    << "\n");
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    // If this function is a known lib function to LLVM then we want to
    // synthesize reference edges to it to model the fact that LLVM can turn
    // arbitrary code into a library function call.
    if (isKnownLibFunction(F, TLI))
      LibFunctions.insert(&F);

    if (F.hasLocalLinkage())
      continue;

    // External linkage defined functions have edges to them from other
    // modules.
    LLVM_DEBUG(dbgs() << "  Adding '" << F.getName()
                      << "' to entry set of the graph.\n");
    addEdge(EntryEdges.Edges, EntryEdges.EdgeIndexMap, get(F), Edge::Ref);
  }

  // Now add entry nodes for functions reachable via initializers to globals.
  SmallVector<Constant *, 16> Worklist;
  SmallPtrSet<Constant *, 16> Visited;
  for (GlobalVariable &GV : M.globals())
    if (GV.hasInitializer())
      if (Visited.insert(GV.getInitializer()).second)
        Worklist.push_back(GV.getInitializer());

  LLVM_DEBUG(
      dbgs() << "  Adding functions referenced by global initializers to the "
                "entry set.\n");
  visitReferences(Worklist, Visited, [&](Function &F) {
    addEdge(EntryEdges.Edges, EntryEdges.EdgeIndexMap, get(F),
            LazyCallGraph::Edge::Ref);
  });
}

LazyCallGraph::LazyCallGraph(LazyCallGraph &&G)
    : BPA(std::move(G.BPA)), NodeMap(std::move(G.NodeMap)),
      EntryEdges(std::move(G.EntryEdges)), SCCBPA(std::move(G.SCCBPA)),
      SCCMap(std::move(G.SCCMap)),
      LibFunctions(std::move(G.LibFunctions)) {
  updateGraphPtrs();
}

LazyCallGraph &LazyCallGraph::operator=(LazyCallGraph &&G) {
  BPA = std::move(G.BPA);
  NodeMap = std::move(G.NodeMap);
  EntryEdges = std::move(G.EntryEdges);
  SCCBPA = std::move(G.SCCBPA);
  SCCMap = std::move(G.SCCMap);
  LibFunctions = std::move(G.LibFunctions);
  updateGraphPtrs();
  return *this;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void LazyCallGraph::SCC::dump() const {
  dbgs() << *this << '\n';
}
#endif

#ifndef NDEBUG
void LazyCallGraph::SCC::verify() {
  assert(OuterRefSCC && "Can't have a null RefSCC!");
  assert(!Nodes.empty() && "Can't have an empty SCC!");

  for (Node *N : Nodes) {
    assert(N && "Can't have a null node!");
    assert(OuterRefSCC->G->lookupSCC(*N) == this &&
           "Node does not map to this SCC!");
    assert(N->DFSNumber == -1 &&
           "Must set DFS numbers to -1 when adding a node to an SCC!");
    assert(N->LowLink == -1 &&
           "Must set low link to -1 when adding a node to an SCC!");
    for (Edge &E : **N)
      assert(E.getNode().isPopulated() && "Can't have an unpopulated node!");
  }
}
#endif

bool LazyCallGraph::SCC::isParentOf(const SCC &C) const {
  if (this == &C)
    return false;

  for (Node &N : *this)
    for (Edge &E : N->calls())
      if (OuterRefSCC->G->lookupSCC(E.getNode()) == &C)
        return true;

  // No edges found.
  return false;
}

bool LazyCallGraph::SCC::isAncestorOf(const SCC &TargetC) const {
  if (this == &TargetC)
    return false;

  LazyCallGraph &G = *OuterRefSCC->G;

  // Start with this SCC.
  SmallPtrSet<const SCC *, 16> Visited = {this};
  SmallVector<const SCC *, 16> Worklist = {this};

  // Walk down the graph until we run out of edges or find a path to TargetC.
  do {
    const SCC &C = *Worklist.pop_back_val();
    for (Node &N : C)
      for (Edge &E : N->calls()) {
        SCC *CalleeC = G.lookupSCC(E.getNode());
        if (!CalleeC)
          continue;

        // If the callee's SCC is the TargetC, we're done.
        if (CalleeC == &TargetC)
          return true;

        // If this is the first time we've reached this SCC, put it on the
        // worklist to recurse through.
        if (Visited.insert(CalleeC).second)
          Worklist.push_back(CalleeC);
      }
  } while (!Worklist.empty());

  // No paths found.
  return false;
}

LazyCallGraph::RefSCC::RefSCC(LazyCallGraph &G) : G(&G) {}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void LazyCallGraph::RefSCC::dump() const {
  dbgs() << *this << '\n';
}
#endif

#ifndef NDEBUG
void LazyCallGraph::RefSCC::verify() {
  assert(G && "Can't have a null graph!");
  assert(!SCCs.empty() && "Can't have an empty SCC!");

  // Verify basic properties of the SCCs.
  SmallPtrSet<SCC *, 4> SCCSet;
  for (SCC *C : SCCs) {
    assert(C && "Can't have a null SCC!");
    C->verify();
    assert(&C->getOuterRefSCC() == this &&
           "SCC doesn't think it is inside this RefSCC!");
    bool Inserted = SCCSet.insert(C).second;
    assert(Inserted && "Found a duplicate SCC!");
    auto IndexIt = SCCIndices.find(C);
    assert(IndexIt != SCCIndices.end() &&
           "Found an SCC that doesn't have an index!");
  }

  // Check that our indices map correctly.
  for (auto &SCCIndexPair : SCCIndices) {
    SCC *C = SCCIndexPair.first;
    int i = SCCIndexPair.second;
    assert(C && "Can't have a null SCC in the indices!");
    assert(SCCSet.count(C) && "Found an index for an SCC not in the RefSCC!");
    assert(SCCs[i] == C && "Index doesn't point to SCC!");
  }

  // Check that the SCCs are in fact in post-order.
  for (int i = 0, Size = SCCs.size(); i < Size; ++i) {
    SCC &SourceSCC = *SCCs[i];
    for (Node &N : SourceSCC)
      for (Edge &E : *N) {
        if (!E.isCall())
          continue;
        SCC &TargetSCC = *G->lookupSCC(E.getNode());
        if (&TargetSCC.getOuterRefSCC() == this) {
          assert(SCCIndices.find(&TargetSCC)->second <= i &&
                 "Edge between SCCs violates post-order relationship.");
          continue;
        }
      }
  }
}
#endif

bool LazyCallGraph::RefSCC::isParentOf(const RefSCC &RC) const {
  if (&RC == this)
    return false;

  // Search all edges to see if this is a parent.
  for (SCC &C : *this)
    for (Node &N : C)
      for (Edge &E : *N)
        if (G->lookupRefSCC(E.getNode()) == &RC)
          return true;

  return false;
}

bool LazyCallGraph::RefSCC::isAncestorOf(const RefSCC &RC) const {
  if (&RC == this)
    return false;

  // For each descendant of this RefSCC, see if one of its children is the
  // argument. If not, add that descendant to the worklist and continue
  // searching.
  SmallVector<const RefSCC *, 4> Worklist;
  SmallPtrSet<const RefSCC *, 4> Visited;
  Worklist.push_back(this);
  Visited.insert(this);
  do {
    const RefSCC &DescendantRC = *Worklist.pop_back_val();
    for (SCC &C : DescendantRC)
      for (Node &N : C)
        for (Edge &E : *N) {
          auto *ChildRC = G->lookupRefSCC(E.getNode());
          if (ChildRC == &RC)
            return true;
          if (!ChildRC || !Visited.insert(ChildRC).second)
            continue;
          Worklist.push_back(ChildRC);
        }
  } while (!Worklist.empty());

  return false;
}

/// Generic helper that updates a postorder sequence of SCCs for a potentially
/// cycle-introducing edge insertion.
///
/// A postorder sequence of SCCs of a directed graph has one fundamental
/// property: all deges in the DAG of SCCs point "up" the sequence. That is,
/// all edges in the SCC DAG point to prior SCCs in the sequence.
///
/// This routine both updates a postorder sequence and uses that sequence to
/// compute the set of SCCs connected into a cycle. It should only be called to
/// insert a "downward" edge which will require changing the sequence to
/// restore it to a postorder.
///
/// When inserting an edge from an earlier SCC to a later SCC in some postorder
/// sequence, all of the SCCs which may be impacted are in the closed range of
/// those two within the postorder sequence. The algorithm used here to restore
/// the state is as follows:
///
/// 1) Starting from the source SCC, construct a set of SCCs which reach the
///    source SCC consisting of just the source SCC. Then scan toward the
///    target SCC in postorder and for each SCC, if it has an edge to an SCC
///    in the set, add it to the set. Otherwise, the source SCC is not
///    a successor, move it in the postorder sequence to immediately before
///    the source SCC, shifting the source SCC and all SCCs in the set one
///    position toward the target SCC. Stop scanning after processing the
///    target SCC.
/// 2) If the source SCC is now past the target SCC in the postorder sequence,
///    and thus the new edge will flow toward the start, we are done.
/// 3) Otherwise, starting from the target SCC, walk all edges which reach an
///    SCC between the source and the target, and add them to the set of
///    connected SCCs, then recurse through them. Once a complete set of the
///    SCCs the target connects to is known, hoist the remaining SCCs between
///    the source and the target to be above the target. Note that there is no
///    need to process the source SCC, it is already known to connect.
/// 4) At this point, all of the SCCs in the closed range between the source
///    SCC and the target SCC in the postorder sequence are connected,
///    including the target SCC and the source SCC. Inserting the edge from
///    the source SCC to the target SCC will form a cycle out of precisely
///    these SCCs. Thus we can merge all of the SCCs in this closed range into
///    a single SCC.
///
/// This process has various important properties:
/// - Only mutates the SCCs when adding the edge actually changes the SCC
///   structure.
/// - Never mutates SCCs which are unaffected by the change.
/// - Updates the postorder sequence to correctly satisfy the postorder
///   constraint after the edge is inserted.
/// - Only reorders SCCs in the closed postorder sequence from the source to
///   the target, so easy to bound how much has changed even in the ordering.
/// - Big-O is the number of edges in the closed postorder range of SCCs from
///   source to target.
///
/// This helper routine, in addition to updating the postorder sequence itself
/// will also update a map from SCCs to indices within that sequence.
///
/// The sequence and the map must operate on pointers to the SCC type.
///
/// Two callbacks must be provided. The first computes the subset of SCCs in
/// the postorder closed range from the source to the target which connect to
/// the source SCC via some (transitive) set of edges. The second computes the
/// subset of the same range which the target SCC connects to via some
/// (transitive) set of edges. Both callbacks should populate the set argument
/// provided.
template <typename SCCT, typename PostorderSequenceT, typename SCCIndexMapT,
          typename ComputeSourceConnectedSetCallableT,
          typename ComputeTargetConnectedSetCallableT>
static iterator_range<typename PostorderSequenceT::iterator>
updatePostorderSequenceForEdgeInsertion(
    SCCT &SourceSCC, SCCT &TargetSCC, PostorderSequenceT &SCCs,
    SCCIndexMapT &SCCIndices,
    ComputeSourceConnectedSetCallableT ComputeSourceConnectedSet,
    ComputeTargetConnectedSetCallableT ComputeTargetConnectedSet) {
  int SourceIdx = SCCIndices[&SourceSCC];
  int TargetIdx = SCCIndices[&TargetSCC];
  assert(SourceIdx < TargetIdx && "Cannot have equal indices here!");

  SmallPtrSet<SCCT *, 4> ConnectedSet;

  // Compute the SCCs which (transitively) reach the source.
  ComputeSourceConnectedSet(ConnectedSet);

  // Partition the SCCs in this part of the port-order sequence so only SCCs
  // connecting to the source remain between it and the target. This is
  // a benign partition as it preserves postorder.
  auto SourceI = std::stable_partition(
      SCCs.begin() + SourceIdx, SCCs.begin() + TargetIdx + 1,
      [&ConnectedSet](SCCT *C) { return !ConnectedSet.count(C); });
  for (int i = SourceIdx, e = TargetIdx + 1; i < e; ++i)
    SCCIndices.find(SCCs[i])->second = i;

  // If the target doesn't connect to the source, then we've corrected the
  // post-order and there are no cycles formed.
  if (!ConnectedSet.count(&TargetSCC)) {
    assert(SourceI > (SCCs.begin() + SourceIdx) &&
           "Must have moved the source to fix the post-order.");
    assert(*std::prev(SourceI) == &TargetSCC &&
           "Last SCC to move should have bene the target.");

    // Return an empty range at the target SCC indicating there is nothing to
    // merge.
    return make_range(std::prev(SourceI), std::prev(SourceI));
  }

  assert(SCCs[TargetIdx] == &TargetSCC &&
         "Should not have moved target if connected!");
  SourceIdx = SourceI - SCCs.begin();
  assert(SCCs[SourceIdx] == &SourceSCC &&
         "Bad updated index computation for the source SCC!");


  // See whether there are any remaining intervening SCCs between the source
  // and target. If so we need to make sure they all are reachable form the
  // target.
  if (SourceIdx + 1 < TargetIdx) {
    ConnectedSet.clear();
    ComputeTargetConnectedSet(ConnectedSet);

    // Partition SCCs so that only SCCs reached from the target remain between
    // the source and the target. This preserves postorder.
    auto TargetI = std::stable_partition(
        SCCs.begin() + SourceIdx + 1, SCCs.begin() + TargetIdx + 1,
        [&ConnectedSet](SCCT *C) { return ConnectedSet.count(C); });
    for (int i = SourceIdx + 1, e = TargetIdx + 1; i < e; ++i)
      SCCIndices.find(SCCs[i])->second = i;
    TargetIdx = std::prev(TargetI) - SCCs.begin();
    assert(SCCs[TargetIdx] == &TargetSCC &&
           "Should always end with the target!");
  }

  // At this point, we know that connecting source to target forms a cycle
  // because target connects back to source, and we know that all of the SCCs
  // between the source and target in the postorder sequence participate in that
  // cycle.
  return make_range(SCCs.begin() + SourceIdx, SCCs.begin() + TargetIdx);
}

bool
LazyCallGraph::RefSCC::switchInternalEdgeToCall(
    Node &SourceN, Node &TargetN,
    function_ref<void(ArrayRef<SCC *> MergeSCCs)> MergeCB) {
  assert(!(*SourceN)[TargetN].isCall() && "Must start with a ref edge!");
  SmallVector<SCC *, 1> DeletedSCCs;

#ifndef NDEBUG
  // In a debug build, verify the RefSCC is valid to start with and when this
  // routine finishes.
  verify();
  auto VerifyOnExit = make_scope_exit([&]() { verify(); });
#endif

  SCC &SourceSCC = *G->lookupSCC(SourceN);
  SCC &TargetSCC = *G->lookupSCC(TargetN);

  // If the two nodes are already part of the same SCC, we're also done as
  // we've just added more connectivity.
  if (&SourceSCC == &TargetSCC) {
    SourceN->setEdgeKind(TargetN, Edge::Call);
    return false; // No new cycle.
  }

  // At this point we leverage the postorder list of SCCs to detect when the
  // insertion of an edge changes the SCC structure in any way.
  //
  // First and foremost, we can eliminate the need for any changes when the
  // edge is toward the beginning of the postorder sequence because all edges
  // flow in that direction already. Thus adding a new one cannot form a cycle.
  int SourceIdx = SCCIndices[&SourceSCC];
  int TargetIdx = SCCIndices[&TargetSCC];
  if (TargetIdx < SourceIdx) {
    SourceN->setEdgeKind(TargetN, Edge::Call);
    return false; // No new cycle.
  }

  // Compute the SCCs which (transitively) reach the source.
  auto ComputeSourceConnectedSet = [&](SmallPtrSetImpl<SCC *> &ConnectedSet) {
#ifndef NDEBUG
    // Check that the RefSCC is still valid before computing this as the
    // results will be nonsensical of we've broken its invariants.
    verify();
#endif
    ConnectedSet.insert(&SourceSCC);
    auto IsConnected = [&](SCC &C) {
      for (Node &N : C)
        for (Edge &E : N->calls())
          if (ConnectedSet.count(G->lookupSCC(E.getNode())))
            return true;

      return false;
    };

    for (SCC *C :
         make_range(SCCs.begin() + SourceIdx + 1, SCCs.begin() + TargetIdx + 1))
      if (IsConnected(*C))
        ConnectedSet.insert(C);
  };

  // Use a normal worklist to find which SCCs the target connects to. We still
  // bound the search based on the range in the postorder list we care about,
  // but because this is forward connectivity we just "recurse" through the
  // edges.
  auto ComputeTargetConnectedSet = [&](SmallPtrSetImpl<SCC *> &ConnectedSet) {
#ifndef NDEBUG
    // Check that the RefSCC is still valid before computing this as the
    // results will be nonsensical of we've broken its invariants.
    verify();
#endif
    ConnectedSet.insert(&TargetSCC);
    SmallVector<SCC *, 4> Worklist;
    Worklist.push_back(&TargetSCC);
    do {
      SCC &C = *Worklist.pop_back_val();
      for (Node &N : C)
        for (Edge &E : *N) {
          if (!E.isCall())
            continue;
          SCC &EdgeC = *G->lookupSCC(E.getNode());
          if (&EdgeC.getOuterRefSCC() != this)
            // Not in this RefSCC...
            continue;
          if (SCCIndices.find(&EdgeC)->second <= SourceIdx)
            // Not in the postorder sequence between source and target.
            continue;

          if (ConnectedSet.insert(&EdgeC).second)
            Worklist.push_back(&EdgeC);
        }
    } while (!Worklist.empty());
  };

  // Use a generic helper to update the postorder sequence of SCCs and return
  // a range of any SCCs connected into a cycle by inserting this edge. This
  // routine will also take care of updating the indices into the postorder
  // sequence.
  auto MergeRange = updatePostorderSequenceForEdgeInsertion(
      SourceSCC, TargetSCC, SCCs, SCCIndices, ComputeSourceConnectedSet,
      ComputeTargetConnectedSet);

  // Run the user's callback on the merged SCCs before we actually merge them.
  if (MergeCB)
    MergeCB(makeArrayRef(MergeRange.begin(), MergeRange.end()));

  // If the merge range is empty, then adding the edge didn't actually form any
  // new cycles. We're done.
  if (empty(MergeRange)) {
    // Now that the SCC structure is finalized, flip the kind to call.
    SourceN->setEdgeKind(TargetN, Edge::Call);
    return false; // No new cycle.
  }

#ifndef NDEBUG
  // Before merging, check that the RefSCC remains valid after all the
  // postorder updates.
  verify();
#endif

  // Otherwise we need to merge all of the SCCs in the cycle into a single
  // result SCC.
  //
  // NB: We merge into the target because all of these functions were already
  // reachable from the target, meaning any SCC-wide properties deduced about it
  // other than the set of functions within it will not have changed.
  for (SCC *C : MergeRange) {
    assert(C != &TargetSCC &&
           "We merge *into* the target and shouldn't process it here!");
    SCCIndices.erase(C);
    TargetSCC.Nodes.append(C->Nodes.begin(), C->Nodes.end());
    for (Node *N : C->Nodes)
      G->SCCMap[N] = &TargetSCC;
    C->clear();
    DeletedSCCs.push_back(C);
  }

  // Erase the merged SCCs from the list and update the indices of the
  // remaining SCCs.
  int IndexOffset = MergeRange.end() - MergeRange.begin();
  auto EraseEnd = SCCs.erase(MergeRange.begin(), MergeRange.end());
  for (SCC *C : make_range(EraseEnd, SCCs.end()))
    SCCIndices[C] -= IndexOffset;

  // Now that the SCC structure is finalized, flip the kind to call.
  SourceN->setEdgeKind(TargetN, Edge::Call);

  // And we're done, but we did form a new cycle.
  return true;
}

void LazyCallGraph::RefSCC::switchTrivialInternalEdgeToRef(Node &SourceN,
                                                           Node &TargetN) {
  assert((*SourceN)[TargetN].isCall() && "Must start with a call edge!");

#ifndef NDEBUG
  // In a debug build, verify the RefSCC is valid to start with and when this
  // routine finishes.
  verify();
  auto VerifyOnExit = make_scope_exit([&]() { verify(); });
#endif

  assert(G->lookupRefSCC(SourceN) == this &&
         "Source must be in this RefSCC.");
  assert(G->lookupRefSCC(TargetN) == this &&
         "Target must be in this RefSCC.");
  assert(G->lookupSCC(SourceN) != G->lookupSCC(TargetN) &&
         "Source and Target must be in separate SCCs for this to be trivial!");

  // Set the edge kind.
  SourceN->setEdgeKind(TargetN, Edge::Ref);
}

iterator_range<LazyCallGraph::RefSCC::iterator>
LazyCallGraph::RefSCC::switchInternalEdgeToRef(Node &SourceN, Node &TargetN) {
  assert((*SourceN)[TargetN].isCall() && "Must start with a call edge!");

#ifndef NDEBUG
  // In a debug build, verify the RefSCC is valid to start with and when this
  // routine finishes.
  verify();
  auto VerifyOnExit = make_scope_exit([&]() { verify(); });
#endif

  assert(G->lookupRefSCC(SourceN) == this &&
         "Source must be in this RefSCC.");
  assert(G->lookupRefSCC(TargetN) == this &&
         "Target must be in this RefSCC.");

  SCC &TargetSCC = *G->lookupSCC(TargetN);
  assert(G->lookupSCC(SourceN) == &TargetSCC && "Source and Target must be in "
                                                "the same SCC to require the "
                                                "full CG update.");

  // Set the edge kind.
  SourceN->setEdgeKind(TargetN, Edge::Ref);

  // Otherwise we are removing a call edge from a single SCC. This may break
  // the cycle. In order to compute the new set of SCCs, we need to do a small
  // DFS over the nodes within the SCC to form any sub-cycles that remain as
  // distinct SCCs and compute a postorder over the resulting SCCs.
  //
  // However, we specially handle the target node. The target node is known to
  // reach all other nodes in the original SCC by definition. This means that
  // we want the old SCC to be replaced with an SCC containing that node as it
  // will be the root of whatever SCC DAG results from the DFS. Assumptions
  // about an SCC such as the set of functions called will continue to hold,
  // etc.

  SCC &OldSCC = TargetSCC;
  SmallVector<std::pair<Node *, EdgeSequence::call_iterator>, 16> DFSStack;
  SmallVector<Node *, 16> PendingSCCStack;
  SmallVector<SCC *, 4> NewSCCs;

  // Prepare the nodes for a fresh DFS.
  SmallVector<Node *, 16> Worklist;
  Worklist.swap(OldSCC.Nodes);
  for (Node *N : Worklist) {
    N->DFSNumber = N->LowLink = 0;
    G->SCCMap.erase(N);
  }

  // Force the target node to be in the old SCC. This also enables us to take
  // a very significant short-cut in the standard Tarjan walk to re-form SCCs
  // below: whenever we build an edge that reaches the target node, we know
  // that the target node eventually connects back to all other nodes in our
  // walk. As a consequence, we can detect and handle participants in that
  // cycle without walking all the edges that form this connection, and instead
  // by relying on the fundamental guarantee coming into this operation (all
  // nodes are reachable from the target due to previously forming an SCC).
  TargetN.DFSNumber = TargetN.LowLink = -1;
  OldSCC.Nodes.push_back(&TargetN);
  G->SCCMap[&TargetN] = &OldSCC;

  // Scan down the stack and DFS across the call edges.
  for (Node *RootN : Worklist) {
    assert(DFSStack.empty() &&
           "Cannot begin a new root with a non-empty DFS stack!");
    assert(PendingSCCStack.empty() &&
           "Cannot begin a new root with pending nodes for an SCC!");

    // Skip any nodes we've already reached in the DFS.
    if (RootN->DFSNumber != 0) {
      assert(RootN->DFSNumber == -1 &&
             "Shouldn't have any mid-DFS root nodes!");
      continue;
    }

    RootN->DFSNumber = RootN->LowLink = 1;
    int NextDFSNumber = 2;

    DFSStack.push_back({RootN, (*RootN)->call_begin()});
    do {
      Node *N;
      EdgeSequence::call_iterator I;
      std::tie(N, I) = DFSStack.pop_back_val();
      auto E = (*N)->call_end();
      while (I != E) {
        Node &ChildN = I->getNode();
        if (ChildN.DFSNumber == 0) {
          // We haven't yet visited this child, so descend, pushing the current
          // node onto the stack.
          DFSStack.push_back({N, I});

          assert(!G->SCCMap.count(&ChildN) &&
                 "Found a node with 0 DFS number but already in an SCC!");
          ChildN.DFSNumber = ChildN.LowLink = NextDFSNumber++;
          N = &ChildN;
          I = (*N)->call_begin();
          E = (*N)->call_end();
          continue;
        }

        // Check for the child already being part of some component.
        if (ChildN.DFSNumber == -1) {
          if (G->lookupSCC(ChildN) == &OldSCC) {
            // If the child is part of the old SCC, we know that it can reach
            // every other node, so we have formed a cycle. Pull the entire DFS
            // and pending stacks into it. See the comment above about setting
            // up the old SCC for why we do this.
            int OldSize = OldSCC.size();
            OldSCC.Nodes.push_back(N);
            OldSCC.Nodes.append(PendingSCCStack.begin(), PendingSCCStack.end());
            PendingSCCStack.clear();
            while (!DFSStack.empty())
              OldSCC.Nodes.push_back(DFSStack.pop_back_val().first);
            for (Node &N : make_range(OldSCC.begin() + OldSize, OldSCC.end())) {
              N.DFSNumber = N.LowLink = -1;
              G->SCCMap[&N] = &OldSCC;
            }
            N = nullptr;
            break;
          }

          // If the child has already been added to some child component, it
          // couldn't impact the low-link of this parent because it isn't
          // connected, and thus its low-link isn't relevant so skip it.
          ++I;
          continue;
        }

        // Track the lowest linked child as the lowest link for this node.
        assert(ChildN.LowLink > 0 && "Must have a positive low-link number!");
        if (ChildN.LowLink < N->LowLink)
          N->LowLink = ChildN.LowLink;

        // Move to the next edge.
        ++I;
      }
      if (!N)
        // Cleared the DFS early, start another round.
        break;

      // We've finished processing N and its descendants, put it on our pending
      // SCC stack to eventually get merged into an SCC of nodes.
      PendingSCCStack.push_back(N);

      // If this node is linked to some lower entry, continue walking up the
      // stack.
      if (N->LowLink != N->DFSNumber)
        continue;

      // Otherwise, we've completed an SCC. Append it to our post order list of
      // SCCs.
      int RootDFSNumber = N->DFSNumber;
      // Find the range of the node stack by walking down until we pass the
      // root DFS number.
      auto SCCNodes = make_range(
          PendingSCCStack.rbegin(),
          find_if(reverse(PendingSCCStack), [RootDFSNumber](const Node *N) {
            return N->DFSNumber < RootDFSNumber;
          }));

      // Form a new SCC out of these nodes and then clear them off our pending
      // stack.
      NewSCCs.push_back(G->createSCC(*this, SCCNodes));
      for (Node &N : *NewSCCs.back()) {
        N.DFSNumber = N.LowLink = -1;
        G->SCCMap[&N] = NewSCCs.back();
      }
      PendingSCCStack.erase(SCCNodes.end().base(), PendingSCCStack.end());
    } while (!DFSStack.empty());
  }

  // Insert the remaining SCCs before the old one. The old SCC can reach all
  // other SCCs we form because it contains the target node of the removed edge
  // of the old SCC. This means that we will have edges into all of the new
  // SCCs, which means the old one must come last for postorder.
  int OldIdx = SCCIndices[&OldSCC];
  SCCs.insert(SCCs.begin() + OldIdx, NewSCCs.begin(), NewSCCs.end());

  // Update the mapping from SCC* to index to use the new SCC*s, and remove the
  // old SCC from the mapping.
  for (int Idx = OldIdx, Size = SCCs.size(); Idx < Size; ++Idx)
    SCCIndices[SCCs[Idx]] = Idx;

  return make_range(SCCs.begin() + OldIdx,
                    SCCs.begin() + OldIdx + NewSCCs.size());
}

void LazyCallGraph::RefSCC::switchOutgoingEdgeToCall(Node &SourceN,
                                                     Node &TargetN) {
  assert(!(*SourceN)[TargetN].isCall() && "Must start with a ref edge!");

  assert(G->lookupRefSCC(SourceN) == this && "Source must be in this RefSCC.");
  assert(G->lookupRefSCC(TargetN) != this &&
         "Target must not be in this RefSCC.");
#ifdef EXPENSIVE_CHECKS
  assert(G->lookupRefSCC(TargetN)->isDescendantOf(*this) &&
         "Target must be a descendant of the Source.");
#endif

  // Edges between RefSCCs are the same regardless of call or ref, so we can
  // just flip the edge here.
  SourceN->setEdgeKind(TargetN, Edge::Call);

#ifndef NDEBUG
  // Check that the RefSCC is still valid.
  verify();
#endif
}

void LazyCallGraph::RefSCC::switchOutgoingEdgeToRef(Node &SourceN,
                                                    Node &TargetN) {
  assert((*SourceN)[TargetN].isCall() && "Must start with a call edge!");

  assert(G->lookupRefSCC(SourceN) == this && "Source must be in this RefSCC.");
  assert(G->lookupRefSCC(TargetN) != this &&
         "Target must not be in this RefSCC.");
#ifdef EXPENSIVE_CHECKS
  assert(G->lookupRefSCC(TargetN)->isDescendantOf(*this) &&
         "Target must be a descendant of the Source.");
#endif

  // Edges between RefSCCs are the same regardless of call or ref, so we can
  // just flip the edge here.
  SourceN->setEdgeKind(TargetN, Edge::Ref);

#ifndef NDEBUG
  // Check that the RefSCC is still valid.
  verify();
#endif
}

void LazyCallGraph::RefSCC::insertInternalRefEdge(Node &SourceN,
                                                  Node &TargetN) {
  assert(G->lookupRefSCC(SourceN) == this && "Source must be in this RefSCC.");
  assert(G->lookupRefSCC(TargetN) == this && "Target must be in this RefSCC.");

  SourceN->insertEdgeInternal(TargetN, Edge::Ref);

#ifndef NDEBUG
  // Check that the RefSCC is still valid.
  verify();
#endif
}

void LazyCallGraph::RefSCC::insertOutgoingEdge(Node &SourceN, Node &TargetN,
                                               Edge::Kind EK) {
  // First insert it into the caller.
  SourceN->insertEdgeInternal(TargetN, EK);

  assert(G->lookupRefSCC(SourceN) == this && "Source must be in this RefSCC.");

  assert(G->lookupRefSCC(TargetN) != this &&
         "Target must not be in this RefSCC.");
#ifdef EXPENSIVE_CHECKS
  assert(G->lookupRefSCC(TargetN)->isDescendantOf(*this) &&
         "Target must be a descendant of the Source.");
#endif

#ifndef NDEBUG
  // Check that the RefSCC is still valid.
  verify();
#endif
}

SmallVector<LazyCallGraph::RefSCC *, 1>
LazyCallGraph::RefSCC::insertIncomingRefEdge(Node &SourceN, Node &TargetN) {
  assert(G->lookupRefSCC(TargetN) == this && "Target must be in this RefSCC.");
  RefSCC &SourceC = *G->lookupRefSCC(SourceN);
  assert(&SourceC != this && "Source must not be in this RefSCC.");
#ifdef EXPENSIVE_CHECKS
  assert(SourceC.isDescendantOf(*this) &&
         "Source must be a descendant of the Target.");
#endif

  SmallVector<RefSCC *, 1> DeletedRefSCCs;

#ifndef NDEBUG
  // In a debug build, verify the RefSCC is valid to start with and when this
  // routine finishes.
  verify();
  auto VerifyOnExit = make_scope_exit([&]() { verify(); });
#endif

  int SourceIdx = G->RefSCCIndices[&SourceC];
  int TargetIdx = G->RefSCCIndices[this];
  assert(SourceIdx < TargetIdx &&
         "Postorder list doesn't see edge as incoming!");

  // Compute the RefSCCs which (transitively) reach the source. We do this by
  // working backwards from the source using the parent set in each RefSCC,
  // skipping any RefSCCs that don't fall in the postorder range. This has the
  // advantage of walking the sparser parent edge (in high fan-out graphs) but
  // more importantly this removes examining all forward edges in all RefSCCs
  // within the postorder range which aren't in fact connected. Only connected
  // RefSCCs (and their edges) are visited here.
  auto ComputeSourceConnectedSet = [&](SmallPtrSetImpl<RefSCC *> &Set) {
    Set.insert(&SourceC);
    auto IsConnected = [&](RefSCC &RC) {
      for (SCC &C : RC)
        for (Node &N : C)
          for (Edge &E : *N)
            if (Set.count(G->lookupRefSCC(E.getNode())))
              return true;

      return false;
    };

    for (RefSCC *C : make_range(G->PostOrderRefSCCs.begin() + SourceIdx + 1,
                                G->PostOrderRefSCCs.begin() + TargetIdx + 1))
      if (IsConnected(*C))
        Set.insert(C);
  };

  // Use a normal worklist to find which SCCs the target connects to. We still
  // bound the search based on the range in the postorder list we care about,
  // but because this is forward connectivity we just "recurse" through the
  // edges.
  auto ComputeTargetConnectedSet = [&](SmallPtrSetImpl<RefSCC *> &Set) {
    Set.insert(this);
    SmallVector<RefSCC *, 4> Worklist;
    Worklist.push_back(this);
    do {
      RefSCC &RC = *Worklist.pop_back_val();
      for (SCC &C : RC)
        for (Node &N : C)
          for (Edge &E : *N) {
            RefSCC &EdgeRC = *G->lookupRefSCC(E.getNode());
            if (G->getRefSCCIndex(EdgeRC) <= SourceIdx)
              // Not in the postorder sequence between source and target.
              continue;

            if (Set.insert(&EdgeRC).second)
              Worklist.push_back(&EdgeRC);
          }
    } while (!Worklist.empty());
  };

  // Use a generic helper to update the postorder sequence of RefSCCs and return
  // a range of any RefSCCs connected into a cycle by inserting this edge. This
  // routine will also take care of updating the indices into the postorder
  // sequence.
  iterator_range<SmallVectorImpl<RefSCC *>::iterator> MergeRange =
      updatePostorderSequenceForEdgeInsertion(
          SourceC, *this, G->PostOrderRefSCCs, G->RefSCCIndices,
          ComputeSourceConnectedSet, ComputeTargetConnectedSet);

  // Build a set so we can do fast tests for whether a RefSCC will end up as
  // part of the merged RefSCC.
  SmallPtrSet<RefSCC *, 16> MergeSet(MergeRange.begin(), MergeRange.end());

  // This RefSCC will always be part of that set, so just insert it here.
  MergeSet.insert(this);

  // Now that we have identified all of the SCCs which need to be merged into
  // a connected set with the inserted edge, merge all of them into this SCC.
  SmallVector<SCC *, 16> MergedSCCs;
  int SCCIndex = 0;
  for (RefSCC *RC : MergeRange) {
    assert(RC != this && "We're merging into the target RefSCC, so it "
                         "shouldn't be in the range.");

    // Walk the inner SCCs to update their up-pointer and walk all the edges to
    // update any parent sets.
    // FIXME: We should try to find a way to avoid this (rather expensive) edge
    // walk by updating the parent sets in some other manner.
    for (SCC &InnerC : *RC) {
      InnerC.OuterRefSCC = this;
      SCCIndices[&InnerC] = SCCIndex++;
      for (Node &N : InnerC)
        G->SCCMap[&N] = &InnerC;
    }

    // Now merge in the SCCs. We can actually move here so try to reuse storage
    // the first time through.
    if (MergedSCCs.empty())
      MergedSCCs = std::move(RC->SCCs);
    else
      MergedSCCs.append(RC->SCCs.begin(), RC->SCCs.end());
    RC->SCCs.clear();
    DeletedRefSCCs.push_back(RC);
  }

  // Append our original SCCs to the merged list and move it into place.
  for (SCC &InnerC : *this)
    SCCIndices[&InnerC] = SCCIndex++;
  MergedSCCs.append(SCCs.begin(), SCCs.end());
  SCCs = std::move(MergedSCCs);

  // Remove the merged away RefSCCs from the post order sequence.
  for (RefSCC *RC : MergeRange)
    G->RefSCCIndices.erase(RC);
  int IndexOffset = MergeRange.end() - MergeRange.begin();
  auto EraseEnd =
      G->PostOrderRefSCCs.erase(MergeRange.begin(), MergeRange.end());
  for (RefSCC *RC : make_range(EraseEnd, G->PostOrderRefSCCs.end()))
    G->RefSCCIndices[RC] -= IndexOffset;

  // At this point we have a merged RefSCC with a post-order SCCs list, just
  // connect the nodes to form the new edge.
  SourceN->insertEdgeInternal(TargetN, Edge::Ref);

  // We return the list of SCCs which were merged so that callers can
  // invalidate any data they have associated with those SCCs. Note that these
  // SCCs are no longer in an interesting state (they are totally empty) but
  // the pointers will remain stable for the life of the graph itself.
  return DeletedRefSCCs;
}

void LazyCallGraph::RefSCC::removeOutgoingEdge(Node &SourceN, Node &TargetN) {
  assert(G->lookupRefSCC(SourceN) == this &&
         "The source must be a member of this RefSCC.");
  assert(G->lookupRefSCC(TargetN) != this &&
         "The target must not be a member of this RefSCC");

#ifndef NDEBUG
  // In a debug build, verify the RefSCC is valid to start with and when this
  // routine finishes.
  verify();
  auto VerifyOnExit = make_scope_exit([&]() { verify(); });
#endif

  // First remove it from the node.
  bool Removed = SourceN->removeEdgeInternal(TargetN);
  (void)Removed;
  assert(Removed && "Target not in the edge set for this caller?");
}

SmallVector<LazyCallGraph::RefSCC *, 1>
LazyCallGraph::RefSCC::removeInternalRefEdge(Node &SourceN,
                                             ArrayRef<Node *> TargetNs) {
  // We return a list of the resulting *new* RefSCCs in post-order.
  SmallVector<RefSCC *, 1> Result;

#ifndef NDEBUG
  // In a debug build, verify the RefSCC is valid to start with and that either
  // we return an empty list of result RefSCCs and this RefSCC remains valid,
  // or we return new RefSCCs and this RefSCC is dead.
  verify();
  auto VerifyOnExit = make_scope_exit([&]() {
    // If we didn't replace our RefSCC with new ones, check that this one
    // remains valid.
    if (G)
      verify();
  });
#endif

  // First remove the actual edges.
  for (Node *TargetN : TargetNs) {
    assert(!(*SourceN)[*TargetN].isCall() &&
           "Cannot remove a call edge, it must first be made a ref edge");

    bool Removed = SourceN->removeEdgeInternal(*TargetN);
    (void)Removed;
    assert(Removed && "Target not in the edge set for this caller?");
  }

  // Direct self references don't impact the ref graph at all.
  if (llvm::all_of(TargetNs,
                   [&](Node *TargetN) { return &SourceN == TargetN; }))
    return Result;

  // If all targets are in the same SCC as the source, because no call edges
  // were removed there is no RefSCC structure change.
  SCC &SourceC = *G->lookupSCC(SourceN);
  if (llvm::all_of(TargetNs, [&](Node *TargetN) {
        return G->lookupSCC(*TargetN) == &SourceC;
      }))
    return Result;

  // We build somewhat synthetic new RefSCCs by providing a postorder mapping
  // for each inner SCC. We store these inside the low-link field of the nodes
  // rather than associated with SCCs because this saves a round-trip through
  // the node->SCC map and in the common case, SCCs are small. We will verify
  // that we always give the same number to every node in the SCC such that
  // these are equivalent.
  int PostOrderNumber = 0;

  // Reset all the other nodes to prepare for a DFS over them, and add them to
  // our worklist.
  SmallVector<Node *, 8> Worklist;
  for (SCC *C : SCCs) {
    for (Node &N : *C)
      N.DFSNumber = N.LowLink = 0;

    Worklist.append(C->Nodes.begin(), C->Nodes.end());
  }

  // Track the number of nodes in this RefSCC so that we can quickly recognize
  // an important special case of the edge removal not breaking the cycle of
  // this RefSCC.
  const int NumRefSCCNodes = Worklist.size();

  SmallVector<std::pair<Node *, EdgeSequence::iterator>, 4> DFSStack;
  SmallVector<Node *, 4> PendingRefSCCStack;
  do {
    assert(DFSStack.empty() &&
           "Cannot begin a new root with a non-empty DFS stack!");
    assert(PendingRefSCCStack.empty() &&
           "Cannot begin a new root with pending nodes for an SCC!");

    Node *RootN = Worklist.pop_back_val();
    // Skip any nodes we've already reached in the DFS.
    if (RootN->DFSNumber != 0) {
      assert(RootN->DFSNumber == -1 &&
             "Shouldn't have any mid-DFS root nodes!");
      continue;
    }

    RootN->DFSNumber = RootN->LowLink = 1;
    int NextDFSNumber = 2;

    DFSStack.push_back({RootN, (*RootN)->begin()});
    do {
      Node *N;
      EdgeSequence::iterator I;
      std::tie(N, I) = DFSStack.pop_back_val();
      auto E = (*N)->end();

      assert(N->DFSNumber != 0 && "We should always assign a DFS number "
                                  "before processing a node.");

      while (I != E) {
        Node &ChildN = I->getNode();
        if (ChildN.DFSNumber == 0) {
          // Mark that we should start at this child when next this node is the
          // top of the stack. We don't start at the next child to ensure this
          // child's lowlink is reflected.
          DFSStack.push_back({N, I});

          // Continue, resetting to the child node.
          ChildN.LowLink = ChildN.DFSNumber = NextDFSNumber++;
          N = &ChildN;
          I = ChildN->begin();
          E = ChildN->end();
          continue;
        }
        if (ChildN.DFSNumber == -1) {
          // If this child isn't currently in this RefSCC, no need to process
          // it.
          ++I;
          continue;
        }

        // Track the lowest link of the children, if any are still in the stack.
        // Any child not on the stack will have a LowLink of -1.
        assert(ChildN.LowLink != 0 &&
               "Low-link must not be zero with a non-zero DFS number.");
        if (ChildN.LowLink >= 0 && ChildN.LowLink < N->LowLink)
          N->LowLink = ChildN.LowLink;
        ++I;
      }

      // We've finished processing N and its descendants, put it on our pending
      // stack to eventually get merged into a RefSCC.
      PendingRefSCCStack.push_back(N);

      // If this node is linked to some lower entry, continue walking up the
      // stack.
      if (N->LowLink != N->DFSNumber) {
        assert(!DFSStack.empty() &&
               "We never found a viable root for a RefSCC to pop off!");
        continue;
      }

      // Otherwise, form a new RefSCC from the top of the pending node stack.
      int RefSCCNumber = PostOrderNumber++;
      int RootDFSNumber = N->DFSNumber;

      // Find the range of the node stack by walking down until we pass the
      // root DFS number. Update the DFS numbers and low link numbers in the
      // process to avoid re-walking this list where possible.
      auto StackRI = find_if(reverse(PendingRefSCCStack), [&](Node *N) {
        if (N->DFSNumber < RootDFSNumber)
          // We've found the bottom.
          return true;

        // Update this node and keep scanning.
        N->DFSNumber = -1;
        // Save the post-order number in the lowlink field so that we can use
        // it to map SCCs into new RefSCCs after we finish the DFS.
        N->LowLink = RefSCCNumber;
        return false;
      });
      auto RefSCCNodes = make_range(StackRI.base(), PendingRefSCCStack.end());

      // If we find a cycle containing all nodes originally in this RefSCC then
      // the removal hasn't changed the structure at all. This is an important
      // special case and we can directly exit the entire routine more
      // efficiently as soon as we discover it.
      if (llvm::size(RefSCCNodes) == NumRefSCCNodes) {
        // Clear out the low link field as we won't need it.
        for (Node *N : RefSCCNodes)
          N->LowLink = -1;
        // Return the empty result immediately.
        return Result;
      }

      // We've already marked the nodes internally with the RefSCC number so
      // just clear them off the stack and continue.
      PendingRefSCCStack.erase(RefSCCNodes.begin(), PendingRefSCCStack.end());
    } while (!DFSStack.empty());

    assert(DFSStack.empty() && "Didn't flush the entire DFS stack!");
    assert(PendingRefSCCStack.empty() && "Didn't flush all pending nodes!");
  } while (!Worklist.empty());

  assert(PostOrderNumber > 1 &&
         "Should never finish the DFS when the existing RefSCC remains valid!");

  // Otherwise we create a collection of new RefSCC nodes and build
  // a radix-sort style map from postorder number to these new RefSCCs. We then
  // append SCCs to each of these RefSCCs in the order they occurred in the
  // original SCCs container.
  for (int i = 0; i < PostOrderNumber; ++i)
    Result.push_back(G->createRefSCC(*G));

  // Insert the resulting postorder sequence into the global graph postorder
  // sequence before the current RefSCC in that sequence, and then remove the
  // current one.
  //
  // FIXME: It'd be nice to change the APIs so that we returned an iterator
  // range over the global postorder sequence and generally use that sequence
  // rather than building a separate result vector here.
  int Idx = G->getRefSCCIndex(*this);
  G->PostOrderRefSCCs.erase(G->PostOrderRefSCCs.begin() + Idx);
  G->PostOrderRefSCCs.insert(G->PostOrderRefSCCs.begin() + Idx, Result.begin(),
                             Result.end());
  for (int i : seq<int>(Idx, G->PostOrderRefSCCs.size()))
    G->RefSCCIndices[G->PostOrderRefSCCs[i]] = i;

  for (SCC *C : SCCs) {
    // We store the SCC number in the node's low-link field above.
    int SCCNumber = C->begin()->LowLink;
    // Clear out all of the SCC's node's low-link fields now that we're done
    // using them as side-storage.
    for (Node &N : *C) {
      assert(N.LowLink == SCCNumber &&
             "Cannot have different numbers for nodes in the same SCC!");
      N.LowLink = -1;
    }

    RefSCC &RC = *Result[SCCNumber];
    int SCCIndex = RC.SCCs.size();
    RC.SCCs.push_back(C);
    RC.SCCIndices[C] = SCCIndex;
    C->OuterRefSCC = &RC;
  }

  // Now that we've moved things into the new RefSCCs, clear out our current
  // one.
  G = nullptr;
  SCCs.clear();
  SCCIndices.clear();

#ifndef NDEBUG
  // Verify the new RefSCCs we've built.
  for (RefSCC *RC : Result)
    RC->verify();
#endif

  // Return the new list of SCCs.
  return Result;
}

void LazyCallGraph::RefSCC::handleTrivialEdgeInsertion(Node &SourceN,
                                                       Node &TargetN) {
  // The only trivial case that requires any graph updates is when we add new
  // ref edge and may connect different RefSCCs along that path. This is only
  // because of the parents set. Every other part of the graph remains constant
  // after this edge insertion.
  assert(G->lookupRefSCC(SourceN) == this && "Source must be in this RefSCC.");
  RefSCC &TargetRC = *G->lookupRefSCC(TargetN);
  if (&TargetRC == this)
    return;

#ifdef EXPENSIVE_CHECKS
  assert(TargetRC.isDescendantOf(*this) &&
         "Target must be a descendant of the Source.");
#endif
}

void LazyCallGraph::RefSCC::insertTrivialCallEdge(Node &SourceN,
                                                  Node &TargetN) {
#ifndef NDEBUG
  // Check that the RefSCC is still valid when we finish.
  auto ExitVerifier = make_scope_exit([this] { verify(); });

#ifdef EXPENSIVE_CHECKS
  // Check that we aren't breaking some invariants of the SCC graph. Note that
  // this is quadratic in the number of edges in the call graph!
  SCC &SourceC = *G->lookupSCC(SourceN);
  SCC &TargetC = *G->lookupSCC(TargetN);
  if (&SourceC != &TargetC)
    assert(SourceC.isAncestorOf(TargetC) &&
           "Call edge is not trivial in the SCC graph!");
#endif // EXPENSIVE_CHECKS
#endif // NDEBUG

  // First insert it into the source or find the existing edge.
  auto InsertResult =
      SourceN->EdgeIndexMap.insert({&TargetN, SourceN->Edges.size()});
  if (!InsertResult.second) {
    // Already an edge, just update it.
    Edge &E = SourceN->Edges[InsertResult.first->second];
    if (E.isCall())
      return; // Nothing to do!
    E.setKind(Edge::Call);
  } else {
    // Create the new edge.
    SourceN->Edges.emplace_back(TargetN, Edge::Call);
  }

  // Now that we have the edge, handle the graph fallout.
  handleTrivialEdgeInsertion(SourceN, TargetN);
}

void LazyCallGraph::RefSCC::insertTrivialRefEdge(Node &SourceN, Node &TargetN) {
#ifndef NDEBUG
  // Check that the RefSCC is still valid when we finish.
  auto ExitVerifier = make_scope_exit([this] { verify(); });

#ifdef EXPENSIVE_CHECKS
  // Check that we aren't breaking some invariants of the RefSCC graph.
  RefSCC &SourceRC = *G->lookupRefSCC(SourceN);
  RefSCC &TargetRC = *G->lookupRefSCC(TargetN);
  if (&SourceRC != &TargetRC)
    assert(SourceRC.isAncestorOf(TargetRC) &&
           "Ref edge is not trivial in the RefSCC graph!");
#endif // EXPENSIVE_CHECKS
#endif // NDEBUG

  // First insert it into the source or find the existing edge.
  auto InsertResult =
      SourceN->EdgeIndexMap.insert({&TargetN, SourceN->Edges.size()});
  if (!InsertResult.second)
    // Already an edge, we're done.
    return;

  // Create the new edge.
  SourceN->Edges.emplace_back(TargetN, Edge::Ref);

  // Now that we have the edge, handle the graph fallout.
  handleTrivialEdgeInsertion(SourceN, TargetN);
}

void LazyCallGraph::RefSCC::replaceNodeFunction(Node &N, Function &NewF) {
  Function &OldF = N.getFunction();

#ifndef NDEBUG
  // Check that the RefSCC is still valid when we finish.
  auto ExitVerifier = make_scope_exit([this] { verify(); });

  assert(G->lookupRefSCC(N) == this &&
         "Cannot replace the function of a node outside this RefSCC.");

  assert(G->NodeMap.find(&NewF) == G->NodeMap.end() &&
         "Must not have already walked the new function!'");

  // It is important that this replacement not introduce graph changes so we
  // insist that the caller has already removed every use of the original
  // function and that all uses of the new function correspond to existing
  // edges in the graph. The common and expected way to use this is when
  // replacing the function itself in the IR without changing the call graph
  // shape and just updating the analysis based on that.
  assert(&OldF != &NewF && "Cannot replace a function with itself!");
  assert(OldF.use_empty() &&
         "Must have moved all uses from the old function to the new!");
#endif

  N.replaceFunction(NewF);

  // Update various call graph maps.
  G->NodeMap.erase(&OldF);
  G->NodeMap[&NewF] = &N;
}

void LazyCallGraph::insertEdge(Node &SourceN, Node &TargetN, Edge::Kind EK) {
  assert(SCCMap.empty() &&
         "This method cannot be called after SCCs have been formed!");

  return SourceN->insertEdgeInternal(TargetN, EK);
}

void LazyCallGraph::removeEdge(Node &SourceN, Node &TargetN) {
  assert(SCCMap.empty() &&
         "This method cannot be called after SCCs have been formed!");

  bool Removed = SourceN->removeEdgeInternal(TargetN);
  (void)Removed;
  assert(Removed && "Target not in the edge set for this caller?");
}

void LazyCallGraph::removeDeadFunction(Function &F) {
  // FIXME: This is unnecessarily restrictive. We should be able to remove
  // functions which recursively call themselves.
  assert(F.use_empty() &&
         "This routine should only be called on trivially dead functions!");

  // We shouldn't remove library functions as they are never really dead while
  // the call graph is in use -- every function definition refers to them.
  assert(!isLibFunction(F) &&
         "Must not remove lib functions from the call graph!");

  auto NI = NodeMap.find(&F);
  if (NI == NodeMap.end())
    // Not in the graph at all!
    return;

  Node &N = *NI->second;
  NodeMap.erase(NI);

  // Remove this from the entry edges if present.
  EntryEdges.removeEdgeInternal(N);

  if (SCCMap.empty()) {
    // No SCCs have been formed, so removing this is fine and there is nothing
    // else necessary at this point but clearing out the node.
    N.clear();
    return;
  }

  // Cannot remove a function which has yet to be visited in the DFS walk, so
  // if we have a node at all then we must have an SCC and RefSCC.
  auto CI = SCCMap.find(&N);
  assert(CI != SCCMap.end() &&
         "Tried to remove a node without an SCC after DFS walk started!");
  SCC &C = *CI->second;
  SCCMap.erase(CI);
  RefSCC &RC = C.getOuterRefSCC();

  // This node must be the only member of its SCC as it has no callers, and
  // that SCC must be the only member of a RefSCC as it has no references.
  // Validate these properties first.
  assert(C.size() == 1 && "Dead functions must be in a singular SCC");
  assert(RC.size() == 1 && "Dead functions must be in a singular RefSCC");

  auto RCIndexI = RefSCCIndices.find(&RC);
  int RCIndex = RCIndexI->second;
  PostOrderRefSCCs.erase(PostOrderRefSCCs.begin() + RCIndex);
  RefSCCIndices.erase(RCIndexI);
  for (int i = RCIndex, Size = PostOrderRefSCCs.size(); i < Size; ++i)
    RefSCCIndices[PostOrderRefSCCs[i]] = i;

  // Finally clear out all the data structures from the node down through the
  // components.
  N.clear();
  N.G = nullptr;
  N.F = nullptr;
  C.clear();
  RC.clear();
  RC.G = nullptr;

  // Nothing to delete as all the objects are allocated in stable bump pointer
  // allocators.
}

LazyCallGraph::Node &LazyCallGraph::insertInto(Function &F, Node *&MappedN) {
  return *new (MappedN = BPA.Allocate()) Node(*this, F);
}

void LazyCallGraph::updateGraphPtrs() {
  // Walk the node map to update their graph pointers. While this iterates in
  // an unstable order, the order has no effect so it remains correct.
  for (auto &FunctionNodePair : NodeMap)
    FunctionNodePair.second->G = this;

  for (auto *RC : PostOrderRefSCCs)
    RC->G = this;
}

template <typename RootsT, typename GetBeginT, typename GetEndT,
          typename GetNodeT, typename FormSCCCallbackT>
void LazyCallGraph::buildGenericSCCs(RootsT &&Roots, GetBeginT &&GetBegin,
                                     GetEndT &&GetEnd, GetNodeT &&GetNode,
                                     FormSCCCallbackT &&FormSCC) {
  using EdgeItT = decltype(GetBegin(std::declval<Node &>()));

  SmallVector<std::pair<Node *, EdgeItT>, 16> DFSStack;
  SmallVector<Node *, 16> PendingSCCStack;

  // Scan down the stack and DFS across the call edges.
  for (Node *RootN : Roots) {
    assert(DFSStack.empty() &&
           "Cannot begin a new root with a non-empty DFS stack!");
    assert(PendingSCCStack.empty() &&
           "Cannot begin a new root with pending nodes for an SCC!");

    // Skip any nodes we've already reached in the DFS.
    if (RootN->DFSNumber != 0) {
      assert(RootN->DFSNumber == -1 &&
             "Shouldn't have any mid-DFS root nodes!");
      continue;
    }

    RootN->DFSNumber = RootN->LowLink = 1;
    int NextDFSNumber = 2;

    DFSStack.push_back({RootN, GetBegin(*RootN)});
    do {
      Node *N;
      EdgeItT I;
      std::tie(N, I) = DFSStack.pop_back_val();
      auto E = GetEnd(*N);
      while (I != E) {
        Node &ChildN = GetNode(I);
        if (ChildN.DFSNumber == 0) {
          // We haven't yet visited this child, so descend, pushing the current
          // node onto the stack.
          DFSStack.push_back({N, I});

          ChildN.DFSNumber = ChildN.LowLink = NextDFSNumber++;
          N = &ChildN;
          I = GetBegin(*N);
          E = GetEnd(*N);
          continue;
        }

        // If the child has already been added to some child component, it
        // couldn't impact the low-link of this parent because it isn't
        // connected, and thus its low-link isn't relevant so skip it.
        if (ChildN.DFSNumber == -1) {
          ++I;
          continue;
        }

        // Track the lowest linked child as the lowest link for this node.
        assert(ChildN.LowLink > 0 && "Must have a positive low-link number!");
        if (ChildN.LowLink < N->LowLink)
          N->LowLink = ChildN.LowLink;

        // Move to the next edge.
        ++I;
      }

      // We've finished processing N and its descendants, put it on our pending
      // SCC stack to eventually get merged into an SCC of nodes.
      PendingSCCStack.push_back(N);

      // If this node is linked to some lower entry, continue walking up the
      // stack.
      if (N->LowLink != N->DFSNumber)
        continue;

      // Otherwise, we've completed an SCC. Append it to our post order list of
      // SCCs.
      int RootDFSNumber = N->DFSNumber;
      // Find the range of the node stack by walking down until we pass the
      // root DFS number.
      auto SCCNodes = make_range(
          PendingSCCStack.rbegin(),
          find_if(reverse(PendingSCCStack), [RootDFSNumber](const Node *N) {
            return N->DFSNumber < RootDFSNumber;
          }));
      // Form a new SCC out of these nodes and then clear them off our pending
      // stack.
      FormSCC(SCCNodes);
      PendingSCCStack.erase(SCCNodes.end().base(), PendingSCCStack.end());
    } while (!DFSStack.empty());
  }
}

/// Build the internal SCCs for a RefSCC from a sequence of nodes.
///
/// Appends the SCCs to the provided vector and updates the map with their
/// indices. Both the vector and map must be empty when passed into this
/// routine.
void LazyCallGraph::buildSCCs(RefSCC &RC, node_stack_range Nodes) {
  assert(RC.SCCs.empty() && "Already built SCCs!");
  assert(RC.SCCIndices.empty() && "Already mapped SCC indices!");

  for (Node *N : Nodes) {
    assert(N->LowLink >= (*Nodes.begin())->LowLink &&
           "We cannot have a low link in an SCC lower than its root on the "
           "stack!");

    // This node will go into the next RefSCC, clear out its DFS and low link
    // as we scan.
    N->DFSNumber = N->LowLink = 0;
  }

  // Each RefSCC contains a DAG of the call SCCs. To build these, we do
  // a direct walk of the call edges using Tarjan's algorithm. We reuse the
  // internal storage as we won't need it for the outer graph's DFS any longer.
  buildGenericSCCs(
      Nodes, [](Node &N) { return N->call_begin(); },
      [](Node &N) { return N->call_end(); },
      [](EdgeSequence::call_iterator I) -> Node & { return I->getNode(); },
      [this, &RC](node_stack_range Nodes) {
        RC.SCCs.push_back(createSCC(RC, Nodes));
        for (Node &N : *RC.SCCs.back()) {
          N.DFSNumber = N.LowLink = -1;
          SCCMap[&N] = RC.SCCs.back();
        }
      });

  // Wire up the SCC indices.
  for (int i = 0, Size = RC.SCCs.size(); i < Size; ++i)
    RC.SCCIndices[RC.SCCs[i]] = i;
}

void LazyCallGraph::buildRefSCCs() {
  if (EntryEdges.empty() || !PostOrderRefSCCs.empty())
    // RefSCCs are either non-existent or already built!
    return;

  assert(RefSCCIndices.empty() && "Already mapped RefSCC indices!");

  SmallVector<Node *, 16> Roots;
  for (Edge &E : *this)
    Roots.push_back(&E.getNode());

  // The roots will be popped of a stack, so use reverse to get a less
  // surprising order. This doesn't change any of the semantics anywhere.
  std::reverse(Roots.begin(), Roots.end());

  buildGenericSCCs(
      Roots,
      [](Node &N) {
        // We need to populate each node as we begin to walk its edges.
        N.populate();
        return N->begin();
      },
      [](Node &N) { return N->end(); },
      [](EdgeSequence::iterator I) -> Node & { return I->getNode(); },
      [this](node_stack_range Nodes) {
        RefSCC *NewRC = createRefSCC(*this);
        buildSCCs(*NewRC, Nodes);

        // Push the new node into the postorder list and remember its position
        // in the index map.
        bool Inserted =
            RefSCCIndices.insert({NewRC, PostOrderRefSCCs.size()}).second;
        (void)Inserted;
        assert(Inserted && "Cannot already have this RefSCC in the index map!");
        PostOrderRefSCCs.push_back(NewRC);
#ifndef NDEBUG
        NewRC->verify();
#endif
      });
}

AnalysisKey LazyCallGraphAnalysis::Key;

LazyCallGraphPrinterPass::LazyCallGraphPrinterPass(raw_ostream &OS) : OS(OS) {}

static void printNode(raw_ostream &OS, LazyCallGraph::Node &N) {
  OS << "  Edges in function: " << N.getFunction().getName() << "\n";
  for (LazyCallGraph::Edge &E : N.populate())
    OS << "    " << (E.isCall() ? "call" : "ref ") << " -> "
       << E.getFunction().getName() << "\n";

  OS << "\n";
}

static void printSCC(raw_ostream &OS, LazyCallGraph::SCC &C) {
  ptrdiff_t Size = size(C);
  OS << "    SCC with " << Size << " functions:\n";

  for (LazyCallGraph::Node &N : C)
    OS << "      " << N.getFunction().getName() << "\n";
}

static void printRefSCC(raw_ostream &OS, LazyCallGraph::RefSCC &C) {
  ptrdiff_t Size = size(C);
  OS << "  RefSCC with " << Size << " call SCCs:\n";

  for (LazyCallGraph::SCC &InnerC : C)
    printSCC(OS, InnerC);

  OS << "\n";
}

PreservedAnalyses LazyCallGraphPrinterPass::run(Module &M,
                                                ModuleAnalysisManager &AM) {
  LazyCallGraph &G = AM.getResult<LazyCallGraphAnalysis>(M);

  OS << "Printing the call graph for module: " << M.getModuleIdentifier()
     << "\n\n";

  for (Function &F : M)
    printNode(OS, G.get(F));

  G.buildRefSCCs();
  for (LazyCallGraph::RefSCC &C : G.postorder_ref_sccs())
    printRefSCC(OS, C);

  return PreservedAnalyses::all();
}

LazyCallGraphDOTPrinterPass::LazyCallGraphDOTPrinterPass(raw_ostream &OS)
    : OS(OS) {}

static void printNodeDOT(raw_ostream &OS, LazyCallGraph::Node &N) {
  std::string Name = "\"" + DOT::EscapeString(N.getFunction().getName()) + "\"";

  for (LazyCallGraph::Edge &E : N.populate()) {
    OS << "  " << Name << " -> \""
       << DOT::EscapeString(E.getFunction().getName()) << "\"";
    if (!E.isCall()) // It is a ref edge.
      OS << " [style=dashed,label=\"ref\"]";
    OS << ";\n";
  }

  OS << "\n";
}

PreservedAnalyses LazyCallGraphDOTPrinterPass::run(Module &M,
                                                   ModuleAnalysisManager &AM) {
  LazyCallGraph &G = AM.getResult<LazyCallGraphAnalysis>(M);

  OS << "digraph \"" << DOT::EscapeString(M.getModuleIdentifier()) << "\" {\n";

  for (Function &F : M)
    printNodeDOT(OS, G.get(F));

  OS << "}\n";

  return PreservedAnalyses::all();
}
