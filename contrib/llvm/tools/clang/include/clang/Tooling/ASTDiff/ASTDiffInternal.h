//===- ASTDiffInternal.h --------------------------------------*- C++ -*- -===//
//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_ASTDIFF_ASTDIFFINTERNAL_H
#define LLVM_CLANG_TOOLING_ASTDIFF_ASTDIFFINTERNAL_H

#include "clang/AST/ASTTypeTraits.h"

namespace clang {
namespace diff {

using DynTypedNode = ast_type_traits::DynTypedNode;

class SyntaxTree;
class SyntaxTreeImpl;
struct ComparisonOptions;

/// Within a tree, this identifies a node by its preorder offset.
struct NodeId {
private:
  static constexpr int InvalidNodeId = -1;

public:
  int Id;

  NodeId() : Id(InvalidNodeId) {}
  NodeId(int Id) : Id(Id) {}

  operator int() const { return Id; }
  NodeId &operator++() { return ++Id, *this; }
  NodeId &operator--() { return --Id, *this; }
  // Support defining iterators on NodeId.
  NodeId &operator*() { return *this; }

  bool isValid() const { return Id != InvalidNodeId; }
  bool isInvalid() const { return Id == InvalidNodeId; }
};

} // end namespace diff
} // end namespace clang
#endif
