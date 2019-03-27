//===- CallGraph.h - AST-based Call graph -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file declares the AST-based CallGraph.
//
//  A call graph for functions whose definitions/bodies are available in the
//  current translation unit. The graph has a "virtual" root node that contains
//  edges to all externally available functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_CALLGRAPH_H
#define LLVM_CLANG_ANALYSIS_CALLGRAPH_H

#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include <memory>

namespace clang {

class CallGraphNode;
class Decl;
class DeclContext;
class Stmt;

/// The AST-based call graph.
///
/// The call graph extends itself with the given declarations by implementing
/// the recursive AST visitor, which constructs the graph by visiting the given
/// declarations.
class CallGraph : public RecursiveASTVisitor<CallGraph> {
  friend class CallGraphNode;

  using FunctionMapTy =
      llvm::DenseMap<const Decl *, std::unique_ptr<CallGraphNode>>;

  /// FunctionMap owns all CallGraphNodes.
  FunctionMapTy FunctionMap;

  /// This is a virtual root node that has edges to all the functions.
  CallGraphNode *Root;

public:
  CallGraph();
  ~CallGraph();

  /// Populate the call graph with the functions in the given
  /// declaration.
  ///
  /// Recursively walks the declaration to find all the dependent Decls as well.
  void addToCallGraph(Decl *D) {
    TraverseDecl(D);
  }

  /// Determine if a declaration should be included in the graph.
  static bool includeInGraph(const Decl *D);

  /// Lookup the node for the given declaration.
  CallGraphNode *getNode(const Decl *) const;

  /// Lookup the node for the given declaration. If none found, insert
  /// one into the graph.
  CallGraphNode *getOrInsertNode(Decl *);

  using iterator = FunctionMapTy::iterator;
  using const_iterator = FunctionMapTy::const_iterator;

  /// Iterators through all the elements in the graph. Note, this gives
  /// non-deterministic order.
  iterator begin() { return FunctionMap.begin(); }
  iterator end()   { return FunctionMap.end();   }
  const_iterator begin() const { return FunctionMap.begin(); }
  const_iterator end()   const { return FunctionMap.end();   }

  /// Get the number of nodes in the graph.
  unsigned size() const { return FunctionMap.size(); }

  /// \ brief Get the virtual root of the graph, all the functions available
  /// externally are represented as callees of the node.
  CallGraphNode *getRoot() const { return Root; }

  /// Iterators through all the nodes of the graph that have no parent. These
  /// are the unreachable nodes, which are either unused or are due to us
  /// failing to add a call edge due to the analysis imprecision.
  using nodes_iterator = llvm::SetVector<CallGraphNode *>::iterator;
  using const_nodes_iterator = llvm::SetVector<CallGraphNode *>::const_iterator;

  void print(raw_ostream &os) const;
  void dump() const;
  void viewGraph() const;

  void addNodesForBlocks(DeclContext *D);

  /// Part of recursive declaration visitation. We recursively visit all the
  /// declarations to collect the root functions.
  bool VisitFunctionDecl(FunctionDecl *FD) {
    // We skip function template definitions, as their semantics is
    // only determined when they are instantiated.
    if (includeInGraph(FD) && FD->isThisDeclarationADefinition()) {
      // Add all blocks declared inside this function to the graph.
      addNodesForBlocks(FD);
      // If this function has external linkage, anything could call it.
      // Note, we are not precise here. For example, the function could have
      // its address taken.
      addNodeForDecl(FD, FD->isGlobal());
    }
    return true;
  }

  /// Part of recursive declaration visitation.
  bool VisitObjCMethodDecl(ObjCMethodDecl *MD) {
    if (includeInGraph(MD)) {
      addNodesForBlocks(MD);
      addNodeForDecl(MD, true);
    }
    return true;
  }

  // We are only collecting the declarations, so do not step into the bodies.
  bool TraverseStmt(Stmt *S) { return true; }

  bool shouldWalkTypesOfTypeLocs() const { return false; }
  bool shouldVisitTemplateInstantiations() const { return true; }

private:
  /// Add the given declaration to the call graph.
  void addNodeForDecl(Decl *D, bool IsGlobal);

  /// Allocate a new node in the graph.
  CallGraphNode *allocateNewNode(Decl *);
};

class CallGraphNode {
public:
  using CallRecord = CallGraphNode *;

private:
  /// The function/method declaration.
  Decl *FD;

  /// The list of functions called from this node.
  SmallVector<CallRecord, 5> CalledFunctions;

public:
  CallGraphNode(Decl *D) : FD(D) {}

  using iterator = SmallVectorImpl<CallRecord>::iterator;
  using const_iterator = SmallVectorImpl<CallRecord>::const_iterator;

  /// Iterators through all the callees/children of the node.
  iterator begin() { return CalledFunctions.begin(); }
  iterator end() { return CalledFunctions.end(); }
  const_iterator begin() const { return CalledFunctions.begin(); }
  const_iterator end() const { return CalledFunctions.end(); }

  bool empty() const { return CalledFunctions.empty(); }
  unsigned size() const { return CalledFunctions.size(); }

  void addCallee(CallGraphNode *N) {
    CalledFunctions.push_back(N);
  }

  Decl *getDecl() const { return FD; }

  void print(raw_ostream &os) const;
  void dump() const;
};

} // namespace clang

// Graph traits for iteration, viewing.
namespace llvm {

template <> struct GraphTraits<clang::CallGraphNode*> {
  using NodeType = clang::CallGraphNode;
  using NodeRef = clang::CallGraphNode *;
  using ChildIteratorType = NodeType::iterator;

  static NodeType *getEntryNode(clang::CallGraphNode *CGN) { return CGN; }
  static ChildIteratorType child_begin(NodeType *N) { return N->begin();  }
  static ChildIteratorType child_end(NodeType *N) { return N->end(); }
};

template <> struct GraphTraits<const clang::CallGraphNode*> {
  using NodeType = const clang::CallGraphNode;
  using NodeRef = const clang::CallGraphNode *;
  using ChildIteratorType = NodeType::const_iterator;

  static NodeType *getEntryNode(const clang::CallGraphNode *CGN) { return CGN; }
  static ChildIteratorType child_begin(NodeType *N) { return N->begin();}
  static ChildIteratorType child_end(NodeType *N) { return N->end(); }
};

template <> struct GraphTraits<clang::CallGraph*>
  : public GraphTraits<clang::CallGraphNode*> {
  static NodeType *getEntryNode(clang::CallGraph *CGN) {
    return CGN->getRoot();  // Start at the external node!
  }

  static clang::CallGraphNode *
  CGGetValue(clang::CallGraph::const_iterator::value_type &P) {
    return P.second.get();
  }

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  using nodes_iterator =
      mapped_iterator<clang::CallGraph::iterator, decltype(&CGGetValue)>;

  static nodes_iterator nodes_begin(clang::CallGraph *CG) {
    return nodes_iterator(CG->begin(), &CGGetValue);
  }

  static nodes_iterator nodes_end  (clang::CallGraph *CG) {
    return nodes_iterator(CG->end(), &CGGetValue);
  }

  static unsigned size(clang::CallGraph *CG) { return CG->size(); }
};

template <> struct GraphTraits<const clang::CallGraph*> :
  public GraphTraits<const clang::CallGraphNode*> {
  static NodeType *getEntryNode(const clang::CallGraph *CGN) {
    return CGN->getRoot();
  }

  static clang::CallGraphNode *
  CGGetValue(clang::CallGraph::const_iterator::value_type &P) {
    return P.second.get();
  }

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  using nodes_iterator =
      mapped_iterator<clang::CallGraph::const_iterator, decltype(&CGGetValue)>;

  static nodes_iterator nodes_begin(const clang::CallGraph *CG) {
    return nodes_iterator(CG->begin(), &CGGetValue);
  }

  static nodes_iterator nodes_end(const clang::CallGraph *CG) {
    return nodes_iterator(CG->end(), &CGGetValue);
  }

  static unsigned size(const clang::CallGraph *CG) { return CG->size(); }
};

} // namespace llvm

#endif // LLVM_CLANG_ANALYSIS_CALLGRAPH_H
