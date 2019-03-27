//===- SelectionDAGAddressAnalysis.h - DAG Address Analysis -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SELECTIONDAGADDRESSANALYSIS_H
#define LLVM_CODEGEN_SELECTIONDAGADDRESSANALYSIS_H

#include "llvm/CodeGen/SelectionDAGNodes.h"
#include <cstdint>

namespace llvm {

class SelectionDAG;

/// Helper struct to parse and store a memory address as base + index + offset.
/// We ignore sign extensions when it is safe to do so.
/// The following two expressions are not equivalent. To differentiate we need
/// to store whether there was a sign extension involved in the index
/// computation.
///  (load (i64 add (i64 copyfromreg %c)
///                 (i64 signextend (add (i8 load %index)
///                                      (i8 1))))
/// vs
///
/// (load (i64 add (i64 copyfromreg %c)
///                (i64 signextend (i32 add (i32 signextend (i8 load %index))
///                                         (i32 1)))))
class BaseIndexOffset {
private:
  SDValue Base;
  SDValue Index;
  int64_t Offset = 0;
  bool IsIndexSignExt = false;

public:
  BaseIndexOffset() = default;
  BaseIndexOffset(SDValue Base, SDValue Index, int64_t Offset,
                  bool IsIndexSignExt)
      : Base(Base), Index(Index), Offset(Offset),
        IsIndexSignExt(IsIndexSignExt) {}

  SDValue getBase() { return Base; }
  SDValue getBase() const { return Base; }
  SDValue getIndex() { return Index; }
  SDValue getIndex() const { return Index; }

  bool equalBaseIndex(const BaseIndexOffset &Other,
                      const SelectionDAG &DAG) const {
    int64_t Off;
    return equalBaseIndex(Other, DAG, Off);
  }

  bool equalBaseIndex(const BaseIndexOffset &Other, const SelectionDAG &DAG,
                      int64_t &Off) const;

  /// Parses tree in Ptr for base, index, offset addresses.
  static BaseIndexOffset match(const LSBaseSDNode *N, const SelectionDAG &DAG);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_SELECTIONDAGADDRESSANALYSIS_H
