//===-- ImportedFunctionsInliningStats.cpp ----------------------*- C++ -*-===//
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

#include "llvm/Transforms/Utils/ImportedFunctionsInliningStatistics.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
using namespace llvm;

ImportedFunctionsInliningStatistics::InlineGraphNode &
ImportedFunctionsInliningStatistics::createInlineGraphNode(const Function &F) {

  auto &ValueLookup = NodesMap[F.getName()];
  if (!ValueLookup) {
    ValueLookup = llvm::make_unique<InlineGraphNode>();
    ValueLookup->Imported = F.getMetadata("thinlto_src_module") != nullptr;
  }
  return *ValueLookup;
}

void ImportedFunctionsInliningStatistics::recordInline(const Function &Caller,
                                                       const Function &Callee) {

  InlineGraphNode &CallerNode = createInlineGraphNode(Caller);
  InlineGraphNode &CalleeNode = createInlineGraphNode(Callee);
  CalleeNode.NumberOfInlines++;

  if (!CallerNode.Imported && !CalleeNode.Imported) {
    // Direct inline from not imported callee to not imported caller, so we
    // don't have to add this to graph. It might be very helpful if you wanna
    // get the inliner statistics in compile step where there are no imported
    // functions. In this case the graph would be empty.
    CalleeNode.NumberOfRealInlines++;
    return;
  }

  CallerNode.InlinedCallees.push_back(&CalleeNode);
  if (!CallerNode.Imported) {
    // We could avoid second lookup, but it would make the code ultra ugly.
    auto It = NodesMap.find(Caller.getName());
    assert(It != NodesMap.end() && "The node should be already there.");
    // Save Caller as a starting node for traversal. The string has to be one
    // from map because Caller can disappear (and function name with it).
    NonImportedCallers.push_back(It->first());
  }
}

void ImportedFunctionsInliningStatistics::setModuleInfo(const Module &M) {
  ModuleName = M.getName();
  for (const auto &F : M.functions()) {
    if (F.isDeclaration())
      continue;
    AllFunctions++;
    ImportedFunctions += int(F.getMetadata("thinlto_src_module") != nullptr);
  }
}
static std::string getStatString(const char *Msg, int32_t Fraction, int32_t All,
                                 const char *PercentageOfMsg,
                                 bool LineEnd = true) {
  double Result = 0;
  if (All != 0)
    Result = 100 * static_cast<double>(Fraction) / All;

  std::stringstream Str;
  Str << std::setprecision(4) << Msg << ": " << Fraction << " [" << Result
      << "% of " << PercentageOfMsg << "]";
  if (LineEnd)
    Str << "\n";
  return Str.str();
}

void ImportedFunctionsInliningStatistics::dump(const bool Verbose) {
  calculateRealInlines();
  NonImportedCallers.clear();

  int32_t InlinedImportedFunctionsCount = 0;
  int32_t InlinedNotImportedFunctionsCount = 0;

  int32_t InlinedImportedFunctionsToImportingModuleCount = 0;
  int32_t InlinedNotImportedFunctionsToImportingModuleCount = 0;

  const auto SortedNodes = getSortedNodes();
  std::string Out;
  Out.reserve(5000);
  raw_string_ostream Ostream(Out);

  Ostream << "------- Dumping inliner stats for [" << ModuleName
          << "] -------\n";

  if (Verbose)
    Ostream << "-- List of inlined functions:\n";

  for (const auto &Node : SortedNodes) {
    assert(Node->second->NumberOfInlines >= Node->second->NumberOfRealInlines);
    if (Node->second->NumberOfInlines == 0)
      continue;

    if (Node->second->Imported) {
      InlinedImportedFunctionsCount++;
      InlinedImportedFunctionsToImportingModuleCount +=
          int(Node->second->NumberOfRealInlines > 0);
    } else {
      InlinedNotImportedFunctionsCount++;
      InlinedNotImportedFunctionsToImportingModuleCount +=
          int(Node->second->NumberOfRealInlines > 0);
    }

    if (Verbose)
      Ostream << "Inlined "
              << (Node->second->Imported ? "imported " : "not imported ")
              << "function [" << Node->first() << "]"
              << ": #inlines = " << Node->second->NumberOfInlines
              << ", #inlines_to_importing_module = "
              << Node->second->NumberOfRealInlines << "\n";
  }

  auto InlinedFunctionsCount =
      InlinedImportedFunctionsCount + InlinedNotImportedFunctionsCount;
  auto NotImportedFuncCount = AllFunctions - ImportedFunctions;
  auto ImportedNotInlinedIntoModule =
      ImportedFunctions - InlinedImportedFunctionsToImportingModuleCount;

  Ostream << "-- Summary:\n"
          << "All functions: " << AllFunctions
          << ", imported functions: " << ImportedFunctions << "\n"
          << getStatString("inlined functions", InlinedFunctionsCount,
                           AllFunctions, "all functions")
          << getStatString("imported functions inlined anywhere",
                           InlinedImportedFunctionsCount, ImportedFunctions,
                           "imported functions")
          << getStatString("imported functions inlined into importing module",
                           InlinedImportedFunctionsToImportingModuleCount,
                           ImportedFunctions, "imported functions",
                           /*LineEnd=*/false)
          << getStatString(", remaining", ImportedNotInlinedIntoModule,
                           ImportedFunctions, "imported functions")
          << getStatString("non-imported functions inlined anywhere",
                           InlinedNotImportedFunctionsCount,
                           NotImportedFuncCount, "non-imported functions")
          << getStatString(
                 "non-imported functions inlined into importing module",
                 InlinedNotImportedFunctionsToImportingModuleCount,
                 NotImportedFuncCount, "non-imported functions");
  Ostream.flush();
  dbgs() << Out;
}

void ImportedFunctionsInliningStatistics::calculateRealInlines() {
  // Removing duplicated Callers.
  llvm::sort(NonImportedCallers);
  NonImportedCallers.erase(
      std::unique(NonImportedCallers.begin(), NonImportedCallers.end()),
      NonImportedCallers.end());

  for (const auto &Name : NonImportedCallers) {
    auto &Node = *NodesMap[Name];
    if (!Node.Visited)
      dfs(Node);
  }
}

void ImportedFunctionsInliningStatistics::dfs(InlineGraphNode &GraphNode) {
  assert(!GraphNode.Visited);
  GraphNode.Visited = true;
  for (auto *const InlinedFunctionNode : GraphNode.InlinedCallees) {
    InlinedFunctionNode->NumberOfRealInlines++;
    if (!InlinedFunctionNode->Visited)
      dfs(*InlinedFunctionNode);
  }
}

ImportedFunctionsInliningStatistics::SortedNodesTy
ImportedFunctionsInliningStatistics::getSortedNodes() {
  SortedNodesTy SortedNodes;
  SortedNodes.reserve(NodesMap.size());
  for (const NodesMapTy::value_type& Node : NodesMap)
    SortedNodes.push_back(&Node);

  llvm::sort(SortedNodes, [&](const SortedNodesTy::value_type &Lhs,
                              const SortedNodesTy::value_type &Rhs) {
    if (Lhs->second->NumberOfInlines != Rhs->second->NumberOfInlines)
      return Lhs->second->NumberOfInlines > Rhs->second->NumberOfInlines;
    if (Lhs->second->NumberOfRealInlines != Rhs->second->NumberOfRealInlines)
      return Lhs->second->NumberOfRealInlines >
             Rhs->second->NumberOfRealInlines;
    return Lhs->first() < Rhs->first();
  });
  return SortedNodes;
}
