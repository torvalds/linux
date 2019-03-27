//===- llvm/IR/UseListOrder.h - LLVM Use List Order -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file has structures and command-line options for preserving use-list
// order.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_USELISTORDER_H
#define LLVM_IR_USELISTORDER_H

#include <cstddef>
#include <vector>

namespace llvm {

class Function;
class Value;

/// Structure to hold a use-list order.
struct UseListOrder {
  const Value *V = nullptr;
  const Function *F = nullptr;
  std::vector<unsigned> Shuffle;

  UseListOrder(const Value *V, const Function *F, size_t ShuffleSize)
      : V(V), F(F), Shuffle(ShuffleSize) {}

  UseListOrder() = default;
  UseListOrder(UseListOrder &&) = default;
  UseListOrder &operator=(UseListOrder &&) = default;
};

using UseListOrderStack = std::vector<UseListOrder>;

} // end namespace llvm

#endif // LLVM_IR_USELISTORDER_H
