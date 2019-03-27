//===- CallGraphSort.cpp --------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// Implementation of Call-Chain Clustering from: Optimizing Function Placement
/// for Large-Scale Data-Center Applications
/// https://research.fb.com/wp-content/uploads/2017/01/cgo2017-hfsort-final1.pdf
///
/// The goal of this algorithm is to improve runtime performance of the final
/// executable by arranging code sections such that page table and i-cache
/// misses are minimized.
///
/// Definitions:
/// * Cluster
///   * An ordered list of input sections which are layed out as a unit. At the
///     beginning of the algorithm each input section has its own cluster and
///     the weight of the cluster is the sum of the weight of all incomming
///     edges.
/// * Call-Chain Clustering (C³) Heuristic
///   * Defines when and how clusters are combined. Pick the highest weighted
///     input section then add it to its most likely predecessor if it wouldn't
///     penalize it too much.
/// * Density
///   * The weight of the cluster divided by the size of the cluster. This is a
///     proxy for the ammount of execution time spent per byte of the cluster.
///
/// It does so given a call graph profile by the following:
/// * Build a weighted call graph from the call graph profile
/// * Sort input sections by weight
/// * For each input section starting with the highest weight
///   * Find its most likely predecessor cluster
///   * Check if the combined cluster would be too large, or would have too low
///     a density.
///   * If not, then combine the clusters.
/// * Sort non-empty clusters by density
///
//===----------------------------------------------------------------------===//

#include "CallGraphSort.h"
#include "OutputSections.h"
#include "SymbolTable.h"
#include "Symbols.h"

using namespace llvm;
using namespace lld;
using namespace lld::elf;

namespace {
struct Edge {
  int From;
  uint64_t Weight;
};

struct Cluster {
  Cluster(int Sec, size_t S) : Sections{Sec}, Size(S) {}

  double getDensity() const {
    if (Size == 0)
      return 0;
    return double(Weight) / double(Size);
  }

  std::vector<int> Sections;
  size_t Size = 0;
  uint64_t Weight = 0;
  uint64_t InitialWeight = 0;
  Edge BestPred = {-1, 0};
};

class CallGraphSort {
public:
  CallGraphSort();

  DenseMap<const InputSectionBase *, int> run();

private:
  std::vector<Cluster> Clusters;
  std::vector<const InputSectionBase *> Sections;

  void groupClusters();
};

// Maximum ammount the combined cluster density can be worse than the original
// cluster to consider merging.
constexpr int MAX_DENSITY_DEGRADATION = 8;

// Maximum cluster size in bytes.
constexpr uint64_t MAX_CLUSTER_SIZE = 1024 * 1024;
} // end anonymous namespace

typedef std::pair<const InputSectionBase *, const InputSectionBase *>
    SectionPair;

// Take the edge list in Config->CallGraphProfile, resolve symbol names to
// Symbols, and generate a graph between InputSections with the provided
// weights.
CallGraphSort::CallGraphSort() {
  MapVector<SectionPair, uint64_t> &Profile = Config->CallGraphProfile;
  DenseMap<const InputSectionBase *, int> SecToCluster;

  auto GetOrCreateNode = [&](const InputSectionBase *IS) -> int {
    auto Res = SecToCluster.insert(std::make_pair(IS, Clusters.size()));
    if (Res.second) {
      Sections.push_back(IS);
      Clusters.emplace_back(Clusters.size(), IS->getSize());
    }
    return Res.first->second;
  };

  // Create the graph.
  for (std::pair<SectionPair, uint64_t> &C : Profile) {
    const auto *FromSB = cast<InputSectionBase>(C.first.first->Repl);
    const auto *ToSB = cast<InputSectionBase>(C.first.second->Repl);
    uint64_t Weight = C.second;

    // Ignore edges between input sections belonging to different output
    // sections.  This is done because otherwise we would end up with clusters
    // containing input sections that can't actually be placed adjacently in the
    // output.  This messes with the cluster size and density calculations.  We
    // would also end up moving input sections in other output sections without
    // moving them closer to what calls them.
    if (FromSB->getOutputSection() != ToSB->getOutputSection())
      continue;

    int From = GetOrCreateNode(FromSB);
    int To = GetOrCreateNode(ToSB);

    Clusters[To].Weight += Weight;

    if (From == To)
      continue;

    // Remember the best edge.
    Cluster &ToC = Clusters[To];
    if (ToC.BestPred.From == -1 || ToC.BestPred.Weight < Weight) {
      ToC.BestPred.From = From;
      ToC.BestPred.Weight = Weight;
    }
  }
  for (Cluster &C : Clusters)
    C.InitialWeight = C.Weight;
}

// It's bad to merge clusters which would degrade the density too much.
static bool isNewDensityBad(Cluster &A, Cluster &B) {
  double NewDensity = double(A.Weight + B.Weight) / double(A.Size + B.Size);
  return NewDensity < A.getDensity() / MAX_DENSITY_DEGRADATION;
}

static void mergeClusters(Cluster &Into, Cluster &From) {
  Into.Sections.insert(Into.Sections.end(), From.Sections.begin(),
                       From.Sections.end());
  Into.Size += From.Size;
  Into.Weight += From.Weight;
  From.Sections.clear();
  From.Size = 0;
  From.Weight = 0;
}

// Group InputSections into clusters using the Call-Chain Clustering heuristic
// then sort the clusters by density.
void CallGraphSort::groupClusters() {
  std::vector<int> SortedSecs(Clusters.size());
  std::vector<Cluster *> SecToCluster(Clusters.size());

  for (size_t I = 0; I < Clusters.size(); ++I) {
    SortedSecs[I] = I;
    SecToCluster[I] = &Clusters[I];
  }

  std::stable_sort(SortedSecs.begin(), SortedSecs.end(), [&](int A, int B) {
    return Clusters[B].getDensity() < Clusters[A].getDensity();
  });

  for (int SI : SortedSecs) {
    // Clusters[SI] is the same as SecToClusters[SI] here because it has not
    // been merged into another cluster yet.
    Cluster &C = Clusters[SI];

    // Don't consider merging if the edge is unlikely.
    if (C.BestPred.From == -1 || C.BestPred.Weight * 10 <= C.InitialWeight)
      continue;

    Cluster *PredC = SecToCluster[C.BestPred.From];
    if (PredC == &C)
      continue;

    if (C.Size + PredC->Size > MAX_CLUSTER_SIZE)
      continue;

    if (isNewDensityBad(*PredC, C))
      continue;

    // NOTE: Consider using a disjoint-set to track section -> cluster mapping
    // if this is ever slow.
    for (int SI : C.Sections)
      SecToCluster[SI] = PredC;

    mergeClusters(*PredC, C);
  }

  // Remove empty or dead nodes. Invalidates all cluster indices.
  llvm::erase_if(Clusters, [](const Cluster &C) {
    return C.Size == 0 || C.Sections.empty();
  });

  // Sort by density.
  std::stable_sort(Clusters.begin(), Clusters.end(),
                   [](const Cluster &A, const Cluster &B) {
                     return A.getDensity() > B.getDensity();
                   });
}

DenseMap<const InputSectionBase *, int> CallGraphSort::run() {
  groupClusters();

  // Generate order.
  DenseMap<const InputSectionBase *, int> OrderMap;
  ssize_t CurOrder = 1;

  for (const Cluster &C : Clusters)
    for (int SecIndex : C.Sections)
      OrderMap[Sections[SecIndex]] = CurOrder++;

  return OrderMap;
}

// Sort sections by the profile data provided by -callgraph-profile-file
//
// This first builds a call graph based on the profile data then merges sections
// according to the C³ huristic. All clusters are then sorted by a density
// metric to further improve locality.
DenseMap<const InputSectionBase *, int> elf::computeCallGraphProfileOrder() {
  return CallGraphSort().run();
}
