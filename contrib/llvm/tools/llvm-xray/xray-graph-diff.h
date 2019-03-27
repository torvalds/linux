//===-- xray-graph-diff.h - XRay Graph Diff Renderer ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Generate a DOT file to represent the difference between the function call
// graph of two differnent traces.
//
//===----------------------------------------------------------------------===//

#ifndef XRAY_GRAPH_DIFF_H
#define XRAY_GRAPH_DIFF_H

#include "xray-graph.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/XRay/Graph.h"

namespace llvm {
namespace xray {

// This class creates a graph representing the difference between two
// xray-graphs And allows you to print it to a dot file, with optional color
// coding.
class GraphDiffRenderer {
  static const int N = 2;

public:
  using StatType = GraphRenderer::StatType;
  using TimeStat = GraphRenderer::TimeStat;

  using GREdgeValueType = GraphRenderer::GraphT::EdgeValueType;
  using GRVertexValueType = GraphRenderer::GraphT::VertexValueType;

  struct EdgeAttribute {
    std::array<const GREdgeValueType *, N> CorrEdgePtr = {};
  };

  struct VertexAttribute {
    std::array<const GRVertexValueType *, N> CorrVertexPtr = {};
  };

  using GraphT = Graph<VertexAttribute, EdgeAttribute, StringRef>;

  class Factory {
    std::array<std::reference_wrapper<const GraphRenderer::GraphT>, N> G;

  public:
    template <typename... Ts> Factory(Ts &... Args) : G{{Args...}} {}

    Expected<GraphDiffRenderer> getGraphDiffRenderer();
  };

private:
  GraphT G;

  GraphDiffRenderer() = default;

public:
  void exportGraphAsDOT(raw_ostream &OS, StatType EdgeLabel = StatType::NONE,
                        StatType EdgeColor = StatType::NONE,
                        StatType VertexLabel = StatType::NONE,
                        StatType VertexColor = StatType::NONE,
                        int TruncLen = 40);

  const GraphT &getGraph() { return G; }
};
} // namespace xray
} // namespace llvm

#endif
