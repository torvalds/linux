//===- CFG.h ----------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides various utilities for inspecting and working with the
/// control flow graph in LLVM IR. This includes generic facilities for
/// iterating successors and predecessors of basic blocks, the successors of
/// specific terminator instructions, etc. It also defines specializations of
/// GraphTraits that allow Function and BasicBlock graphs to be treated as
/// proper graphs for generic algorithms.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CFG_H
#define LLVM_IR_CFG_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/type_traits.h"
#include <cassert>
#include <cstddef>
#include <iterator>

namespace llvm {

//===----------------------------------------------------------------------===//
// BasicBlock pred_iterator definition
//===----------------------------------------------------------------------===//

template <class Ptr, class USE_iterator> // Predecessor Iterator
class PredIterator : public std::iterator<std::forward_iterator_tag,
                                          Ptr, ptrdiff_t, Ptr*, Ptr*> {
  using super =
      std::iterator<std::forward_iterator_tag, Ptr, ptrdiff_t, Ptr*, Ptr*>;
  using Self = PredIterator<Ptr, USE_iterator>;
  USE_iterator It;

  inline void advancePastNonTerminators() {
    // Loop to ignore non-terminator uses (for example BlockAddresses).
    while (!It.atEnd()) {
      if (auto *Inst = dyn_cast<Instruction>(*It))
        if (Inst->isTerminator())
          break;

      ++It;
    }
  }

public:
  using pointer = typename super::pointer;
  using reference = typename super::reference;

  PredIterator() = default;
  explicit inline PredIterator(Ptr *bb) : It(bb->user_begin()) {
    advancePastNonTerminators();
  }
  inline PredIterator(Ptr *bb, bool) : It(bb->user_end()) {}

  inline bool operator==(const Self& x) const { return It == x.It; }
  inline bool operator!=(const Self& x) const { return !operator==(x); }

  inline reference operator*() const {
    assert(!It.atEnd() && "pred_iterator out of range!");
    return cast<Instruction>(*It)->getParent();
  }
  inline pointer *operator->() const { return &operator*(); }

  inline Self& operator++() {   // Preincrement
    assert(!It.atEnd() && "pred_iterator out of range!");
    ++It; advancePastNonTerminators();
    return *this;
  }

  inline Self operator++(int) { // Postincrement
    Self tmp = *this; ++*this; return tmp;
  }

  /// getOperandNo - Return the operand number in the predecessor's
  /// terminator of the successor.
  unsigned getOperandNo() const {
    return It.getOperandNo();
  }

  /// getUse - Return the operand Use in the predecessor's terminator
  /// of the successor.
  Use &getUse() const {
    return It.getUse();
  }
};

using pred_iterator = PredIterator<BasicBlock, Value::user_iterator>;
using const_pred_iterator =
    PredIterator<const BasicBlock, Value::const_user_iterator>;
using pred_range = iterator_range<pred_iterator>;
using pred_const_range = iterator_range<const_pred_iterator>;

inline pred_iterator pred_begin(BasicBlock *BB) { return pred_iterator(BB); }
inline const_pred_iterator pred_begin(const BasicBlock *BB) {
  return const_pred_iterator(BB);
}
inline pred_iterator pred_end(BasicBlock *BB) { return pred_iterator(BB, true);}
inline const_pred_iterator pred_end(const BasicBlock *BB) {
  return const_pred_iterator(BB, true);
}
inline bool pred_empty(const BasicBlock *BB) {
  return pred_begin(BB) == pred_end(BB);
}
/// Get the number of predecessors of \p BB. This is a linear time operation.
/// Use \ref BasicBlock::hasNPredecessors() or hasNPredecessorsOrMore if able.
inline unsigned pred_size(const BasicBlock *BB) {
  return std::distance(pred_begin(BB), pred_end(BB));
}
inline pred_range predecessors(BasicBlock *BB) {
  return pred_range(pred_begin(BB), pred_end(BB));
}
inline pred_const_range predecessors(const BasicBlock *BB) {
  return pred_const_range(pred_begin(BB), pred_end(BB));
}

//===----------------------------------------------------------------------===//
// Instruction and BasicBlock succ_iterator helpers
//===----------------------------------------------------------------------===//

template <class InstructionT, class BlockT>
class SuccIterator
    : public iterator_facade_base<SuccIterator<InstructionT, BlockT>,
                                  std::random_access_iterator_tag, BlockT, int,
                                  BlockT *, BlockT *> {
public:
  using difference_type = int;
  using pointer = BlockT *;
  using reference = BlockT *;

private:
  InstructionT *Inst;
  int Idx;
  using Self = SuccIterator<InstructionT, BlockT>;

  inline bool index_is_valid(int Idx) {
    // Note that we specially support the index of zero being valid even in the
    // face of a null instruction.
    return Idx >= 0 && (Idx == 0 || Idx <= (int)Inst->getNumSuccessors());
  }

  /// Proxy object to allow write access in operator[]
  class SuccessorProxy {
    Self It;

  public:
    explicit SuccessorProxy(const Self &It) : It(It) {}

    SuccessorProxy(const SuccessorProxy &) = default;

    SuccessorProxy &operator=(SuccessorProxy RHS) {
      *this = reference(RHS);
      return *this;
    }

    SuccessorProxy &operator=(reference RHS) {
      It.Inst->setSuccessor(It.Idx, RHS);
      return *this;
    }

    operator reference() const { return *It; }
  };

public:
  // begin iterator
  explicit inline SuccIterator(InstructionT *Inst) : Inst(Inst), Idx(0) {}
  // end iterator
  inline SuccIterator(InstructionT *Inst, bool) : Inst(Inst) {
    if (Inst)
      Idx = Inst->getNumSuccessors();
    else
      // Inst == NULL happens, if a basic block is not fully constructed and
      // consequently getTerminator() returns NULL. In this case we construct
      // a SuccIterator which describes a basic block that has zero
      // successors.
      // Defining SuccIterator for incomplete and malformed CFGs is especially
      // useful for debugging.
      Idx = 0;
  }

  /// This is used to interface between code that wants to
  /// operate on terminator instructions directly.
  int getSuccessorIndex() const { return Idx; }

  inline bool operator==(const Self &x) const { return Idx == x.Idx; }

  inline BlockT *operator*() const { return Inst->getSuccessor(Idx); }

  // We use the basic block pointer directly for operator->.
  inline BlockT *operator->() const { return operator*(); }

  inline bool operator<(const Self &RHS) const {
    assert(Inst == RHS.Inst && "Cannot compare iterators of different blocks!");
    return Idx < RHS.Idx;
  }

  int operator-(const Self &RHS) const {
    assert(Inst == RHS.Inst && "Cannot compare iterators of different blocks!");
    return Idx - RHS.Idx;
  }

  inline Self &operator+=(int RHS) {
    int NewIdx = Idx + RHS;
    assert(index_is_valid(NewIdx) && "Iterator index out of bound");
    Idx = NewIdx;
    return *this;
  }

  inline Self &operator-=(int RHS) { return operator+=(-RHS); }

  // Specially implement the [] operation using a proxy object to support
  // assignment.
  inline SuccessorProxy operator[](int Offset) {
    Self TmpIt = *this;
    TmpIt += Offset;
    return SuccessorProxy(TmpIt);
  }

  /// Get the source BlockT of this iterator.
  inline BlockT *getSource() {
    assert(Inst && "Source not available, if basic block was malformed");
    return Inst->getParent();
  }
};

template <typename T, typename U> struct isPodLike<SuccIterator<T, U>> {
  static const bool value = isPodLike<T>::value;
};

using succ_iterator = SuccIterator<Instruction, BasicBlock>;
using succ_const_iterator = SuccIterator<const Instruction, const BasicBlock>;
using succ_range = iterator_range<succ_iterator>;
using succ_const_range = iterator_range<succ_const_iterator>;

inline succ_iterator succ_begin(Instruction *I) { return succ_iterator(I); }
inline succ_const_iterator succ_begin(const Instruction *I) {
  return succ_const_iterator(I);
}
inline succ_iterator succ_end(Instruction *I) { return succ_iterator(I, true); }
inline succ_const_iterator succ_end(const Instruction *I) {
  return succ_const_iterator(I, true);
}
inline bool succ_empty(const Instruction *I) {
  return succ_begin(I) == succ_end(I);
}
inline unsigned succ_size(const Instruction *I) {
  return std::distance(succ_begin(I), succ_end(I));
}
inline succ_range successors(Instruction *I) {
  return succ_range(succ_begin(I), succ_end(I));
}
inline succ_const_range successors(const Instruction *I) {
  return succ_const_range(succ_begin(I), succ_end(I));
}

inline succ_iterator succ_begin(BasicBlock *BB) {
  return succ_iterator(BB->getTerminator());
}
inline succ_const_iterator succ_begin(const BasicBlock *BB) {
  return succ_const_iterator(BB->getTerminator());
}
inline succ_iterator succ_end(BasicBlock *BB) {
  return succ_iterator(BB->getTerminator(), true);
}
inline succ_const_iterator succ_end(const BasicBlock *BB) {
  return succ_const_iterator(BB->getTerminator(), true);
}
inline bool succ_empty(const BasicBlock *BB) {
  return succ_begin(BB) == succ_end(BB);
}
inline unsigned succ_size(const BasicBlock *BB) {
  return std::distance(succ_begin(BB), succ_end(BB));
}
inline succ_range successors(BasicBlock *BB) {
  return succ_range(succ_begin(BB), succ_end(BB));
}
inline succ_const_range successors(const BasicBlock *BB) {
  return succ_const_range(succ_begin(BB), succ_end(BB));
}

//===--------------------------------------------------------------------===//
// GraphTraits specializations for basic block graphs (CFGs)
//===--------------------------------------------------------------------===//

// Provide specializations of GraphTraits to be able to treat a function as a
// graph of basic blocks...

template <> struct GraphTraits<BasicBlock*> {
  using NodeRef = BasicBlock *;
  using ChildIteratorType = succ_iterator;

  static NodeRef getEntryNode(BasicBlock *BB) { return BB; }
  static ChildIteratorType child_begin(NodeRef N) { return succ_begin(N); }
  static ChildIteratorType child_end(NodeRef N) { return succ_end(N); }
};

template <> struct GraphTraits<const BasicBlock*> {
  using NodeRef = const BasicBlock *;
  using ChildIteratorType = succ_const_iterator;

  static NodeRef getEntryNode(const BasicBlock *BB) { return BB; }

  static ChildIteratorType child_begin(NodeRef N) { return succ_begin(N); }
  static ChildIteratorType child_end(NodeRef N) { return succ_end(N); }
};

// Provide specializations of GraphTraits to be able to treat a function as a
// graph of basic blocks... and to walk it in inverse order.  Inverse order for
// a function is considered to be when traversing the predecessor edges of a BB
// instead of the successor edges.
//
template <> struct GraphTraits<Inverse<BasicBlock*>> {
  using NodeRef = BasicBlock *;
  using ChildIteratorType = pred_iterator;

  static NodeRef getEntryNode(Inverse<BasicBlock *> G) { return G.Graph; }
  static ChildIteratorType child_begin(NodeRef N) { return pred_begin(N); }
  static ChildIteratorType child_end(NodeRef N) { return pred_end(N); }
};

template <> struct GraphTraits<Inverse<const BasicBlock*>> {
  using NodeRef = const BasicBlock *;
  using ChildIteratorType = const_pred_iterator;

  static NodeRef getEntryNode(Inverse<const BasicBlock *> G) { return G.Graph; }
  static ChildIteratorType child_begin(NodeRef N) { return pred_begin(N); }
  static ChildIteratorType child_end(NodeRef N) { return pred_end(N); }
};

//===--------------------------------------------------------------------===//
// GraphTraits specializations for function basic block graphs (CFGs)
//===--------------------------------------------------------------------===//

// Provide specializations of GraphTraits to be able to treat a function as a
// graph of basic blocks... these are the same as the basic block iterators,
// except that the root node is implicitly the first node of the function.
//
template <> struct GraphTraits<Function*> : public GraphTraits<BasicBlock*> {
  static NodeRef getEntryNode(Function *F) { return &F->getEntryBlock(); }

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  using nodes_iterator = pointer_iterator<Function::iterator>;

  static nodes_iterator nodes_begin(Function *F) {
    return nodes_iterator(F->begin());
  }

  static nodes_iterator nodes_end(Function *F) {
    return nodes_iterator(F->end());
  }

  static size_t size(Function *F) { return F->size(); }
};
template <> struct GraphTraits<const Function*> :
  public GraphTraits<const BasicBlock*> {
  static NodeRef getEntryNode(const Function *F) { return &F->getEntryBlock(); }

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  using nodes_iterator = pointer_iterator<Function::const_iterator>;

  static nodes_iterator nodes_begin(const Function *F) {
    return nodes_iterator(F->begin());
  }

  static nodes_iterator nodes_end(const Function *F) {
    return nodes_iterator(F->end());
  }

  static size_t size(const Function *F) { return F->size(); }
};

// Provide specializations of GraphTraits to be able to treat a function as a
// graph of basic blocks... and to walk it in inverse order.  Inverse order for
// a function is considered to be when traversing the predecessor edges of a BB
// instead of the successor edges.
//
template <> struct GraphTraits<Inverse<Function*>> :
  public GraphTraits<Inverse<BasicBlock*>> {
  static NodeRef getEntryNode(Inverse<Function *> G) {
    return &G.Graph->getEntryBlock();
  }
};
template <> struct GraphTraits<Inverse<const Function*>> :
  public GraphTraits<Inverse<const BasicBlock*>> {
  static NodeRef getEntryNode(Inverse<const Function *> G) {
    return &G.Graph->getEntryBlock();
  }
};

} // end namespace llvm

#endif // LLVM_IR_CFG_H
