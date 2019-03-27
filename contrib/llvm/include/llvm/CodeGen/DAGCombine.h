//===-- llvm/CodeGen/DAGCombine.h  ------- SelectionDAG Nodes ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//

#ifndef LLVM_CODEGEN_DAGCOMBINE_H
#define LLVM_CODEGEN_DAGCOMBINE_H

namespace llvm {

enum CombineLevel {
  BeforeLegalizeTypes,
  AfterLegalizeTypes,
  AfterLegalizeVectorOps,
  AfterLegalizeDAG
};

} // end llvm namespace

#endif
