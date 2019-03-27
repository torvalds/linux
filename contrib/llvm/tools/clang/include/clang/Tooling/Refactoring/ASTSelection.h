//===--- ASTSelection.h - Clang refactoring library -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_REFACTOR_AST_SELECTION_H
#define LLVM_CLANG_TOOLING_REFACTOR_AST_SELECTION_H

#include "clang/AST/ASTTypeTraits.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include <vector>

namespace clang {

class ASTContext;

namespace tooling {

enum class SourceSelectionKind {
  /// A node that's not selected.
  None,

  /// A node that's considered to be selected because the whole selection range
  /// is inside of its source range.
  ContainsSelection,
  /// A node that's considered to be selected because the start of the selection
  /// range is inside its source range.
  ContainsSelectionStart,
  /// A node that's considered to be selected because the end of the selection
  /// range is inside its source range.
  ContainsSelectionEnd,

  /// A node that's considered to be selected because the node is entirely in
  /// the selection range.
  InsideSelection,
};

/// Represents a selected AST node.
///
/// AST selection is represented using a tree of \c SelectedASTNode. The tree
/// follows the top-down shape of the actual AST. Each selected node has
/// a selection kind. The kind might be none as the node itself might not
/// actually be selected, e.g. a statement in macro whose child is in a macro
/// argument.
struct SelectedASTNode {
  ast_type_traits::DynTypedNode Node;
  SourceSelectionKind SelectionKind;
  std::vector<SelectedASTNode> Children;

  SelectedASTNode(const ast_type_traits::DynTypedNode &Node,
                  SourceSelectionKind SelectionKind)
      : Node(Node), SelectionKind(SelectionKind) {}
  SelectedASTNode(SelectedASTNode &&) = default;
  SelectedASTNode &operator=(SelectedASTNode &&) = default;

  void dump(llvm::raw_ostream &OS = llvm::errs()) const;

  using ReferenceType = std::reference_wrapper<const SelectedASTNode>;
};

/// Traverses the given ASTContext and creates a tree of selected AST nodes.
///
/// \returns None if no nodes are selected in the AST, or a selected AST node
/// that corresponds to the TranslationUnitDecl otherwise.
Optional<SelectedASTNode> findSelectedASTNodes(const ASTContext &Context,
                                               SourceRange SelectionRange);

/// An AST selection value that corresponds to a selection of a set of
/// statements that belong to one body of code (like one function).
///
/// For example, the following selection in the source.
///
/// \code
/// void function() {
///  // selection begin:
///  int x = 0;
///  {
///     // selection end
///     x = 1;
///  }
///  x = 2;
/// }
/// \endcode
///
/// Would correspond to a code range selection of statements "int x = 0"
/// and the entire compound statement that follows it.
///
/// A \c CodeRangeASTSelection value stores references to the full
/// \c SelectedASTNode tree and should not outlive it.
class CodeRangeASTSelection {
public:
  CodeRangeASTSelection(CodeRangeASTSelection &&) = default;
  CodeRangeASTSelection &operator=(CodeRangeASTSelection &&) = default;

  /// Returns the parent hierarchy (top to bottom) for the selected nodes.
  ArrayRef<SelectedASTNode::ReferenceType> getParents() { return Parents; }

  /// Returns the number of selected statements.
  size_t size() const {
    if (!AreChildrenSelected)
      return 1;
    return SelectedNode.get().Children.size();
  }

  const Stmt *operator[](size_t I) const {
    if (!AreChildrenSelected) {
      assert(I == 0 && "Invalid index");
      return SelectedNode.get().Node.get<Stmt>();
    }
    return SelectedNode.get().Children[I].Node.get<Stmt>();
  }

  /// Returns true when a selected code range is in a function-like body
  /// of code, like a function, method or a block.
  ///
  /// This function can be used to test against selected expressions that are
  /// located outside of a function, e.g. global variable initializers, default
  /// argument values, or even template arguments.
  ///
  /// Use the \c getFunctionLikeNearestParent to get the function-like parent
  /// declaration.
  bool isInFunctionLikeBodyOfCode() const;

  /// Returns the nearest function-like parent declaration or null if such
  /// declaration doesn't exist.
  const Decl *getFunctionLikeNearestParent() const;

  static Optional<CodeRangeASTSelection>
  create(SourceRange SelectionRange, const SelectedASTNode &ASTSelection);

private:
  CodeRangeASTSelection(SelectedASTNode::ReferenceType SelectedNode,
                        ArrayRef<SelectedASTNode::ReferenceType> Parents,
                        bool AreChildrenSelected)
      : SelectedNode(SelectedNode), Parents(Parents.begin(), Parents.end()),
        AreChildrenSelected(AreChildrenSelected) {}

  /// The reference to the selected node (or reference to the selected
  /// child nodes).
  SelectedASTNode::ReferenceType SelectedNode;
  /// The parent hierarchy (top to bottom) for the selected noe.
  llvm::SmallVector<SelectedASTNode::ReferenceType, 8> Parents;
  /// True only when the children of the selected node are actually selected.
  bool AreChildrenSelected;
};

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_REFACTOR_AST_SELECTION_H
