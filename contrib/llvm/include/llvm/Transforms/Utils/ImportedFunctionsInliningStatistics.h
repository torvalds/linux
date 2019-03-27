//===-- ImportedFunctionsInliningStats.h ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Generating inliner statistics for imported functions, mostly useful for
// ThinLTO.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_IMPORTEDFUNCTIONSINLININGSTATISTICS_H
#define LLVM_TRANSFORMS_UTILS_IMPORTEDFUNCTIONSINLININGSTATISTICS_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include <string>
#include <vector>

namespace llvm {
class Module;
class Function;
/// Calculate and dump ThinLTO specific inliner stats.
/// The main statistics are:
/// (1) Number of inlined imported functions,
/// (2) Number of imported functions inlined into importing module (indirect),
/// (3) Number of non imported functions inlined into importing module
/// (indirect).
/// The difference between first and the second is that first stat counts
/// all performed inlines on imported functions, but the second one only the
/// functions that have been eventually inlined to a function in the importing
/// module (by a chain of inlines). Because llvm uses bottom-up inliner, it is
/// possible to e.g. import function `A`, `B` and then inline `B` to `A`,
/// and after this `A` might be too big to be inlined into some other function
/// that calls it. It calculates this statistic by building graph, where
/// the nodes are functions, and edges are performed inlines and then by marking
/// the edges starting from not imported function.
///
/// If `Verbose` is set to true, then it also dumps statistics
/// per each inlined function, sorted by the greatest inlines count like
/// - number of performed inlines
/// - number of performed inlines to importing module
class ImportedFunctionsInliningStatistics {
private:
  /// InlineGraphNode represents node in graph of inlined functions.
  struct InlineGraphNode {
    // Default-constructible and movable.
    InlineGraphNode() = default;
    InlineGraphNode(InlineGraphNode &&) = default;
    InlineGraphNode &operator=(InlineGraphNode &&) = default;

    llvm::SmallVector<InlineGraphNode *, 8> InlinedCallees;
    /// Incremented every direct inline.
    int32_t NumberOfInlines = 0;
    /// Number of inlines into non imported function (possibly indirect via
    /// intermediate inlines). Computed based on graph search.
    int32_t NumberOfRealInlines = 0;
    bool Imported = false;
    bool Visited = false;
  };

public:
  ImportedFunctionsInliningStatistics() = default;
  ImportedFunctionsInliningStatistics(
      const ImportedFunctionsInliningStatistics &) = delete;

  /// Set information like AllFunctions, ImportedFunctions, ModuleName.
  void setModuleInfo(const Module &M);
  /// Record inline of @param Callee to @param Caller for statistis.
  void recordInline(const Function &Caller, const Function &Callee);
  /// Dump stats computed with InlinerStatistics class.
  /// If @param Verbose is true then separate statistics for every inlined
  /// function will be printed.
  void dump(bool Verbose);

private:
  /// Creates new Node in NodeMap and sets attributes, or returns existed one.
  InlineGraphNode &createInlineGraphNode(const Function &);
  void calculateRealInlines();
  void dfs(InlineGraphNode &GraphNode);

  using NodesMapTy =
      llvm::StringMap<std::unique_ptr<InlineGraphNode>>;
  using SortedNodesTy =
      std::vector<const NodesMapTy::MapEntryTy*>;
  /// Returns vector of elements sorted by
  /// (-NumberOfInlines, -NumberOfRealInlines, FunctionName).
  SortedNodesTy getSortedNodes();

private:
  /// This map manage life of all InlineGraphNodes. Unique pointer to
  /// InlineGraphNode used since the node pointers are also saved in the
  /// InlinedCallees vector. If it would store InlineGraphNode instead then the
  /// address of the node would not be invariant.
  NodesMapTy NodesMap;
  /// Non external functions that have some other function inlined inside.
  std::vector<StringRef> NonImportedCallers;
  int AllFunctions = 0;
  int ImportedFunctions = 0;
  StringRef ModuleName;
};

} // llvm

#endif // LLVM_TRANSFORMS_UTILS_IMPORTEDFUNCTIONSINLININGSTATISTICS_H
