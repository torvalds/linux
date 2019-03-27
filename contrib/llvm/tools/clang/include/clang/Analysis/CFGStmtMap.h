//===--- CFGStmtMap.h - Map from Stmt* to CFGBlock* -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the CFGStmtMap class, which defines a mapping from
//  Stmt* to CFGBlock*
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_CFGSTMTMAP_H
#define LLVM_CLANG_ANALYSIS_CFGSTMTMAP_H

#include "clang/Analysis/CFG.h"

namespace clang {

class ParentMap;
class Stmt;

class CFGStmtMap {
  ParentMap *PM;
  void *M;

  CFGStmtMap(ParentMap *pm, void *m) : PM(pm), M(m) {}

public:
  ~CFGStmtMap();

  /// Returns a new CFGMap for the given CFG.  It is the caller's
  /// responsibility to 'delete' this object when done using it.
  static CFGStmtMap *Build(CFG* C, ParentMap *PM);

  /// Returns the CFGBlock the specified Stmt* appears in.  For Stmt* that
  /// are terminators, the CFGBlock is the block they appear as a terminator,
  /// and not the block they appear as a block-level expression (e.g, '&&').
  /// CaseStmts and LabelStmts map to the CFGBlock they label.
  CFGBlock *getBlock(Stmt * S);

  const CFGBlock *getBlock(const Stmt * S) const {
    return const_cast<CFGStmtMap*>(this)->getBlock(const_cast<Stmt*>(S));
  }
};

} // end clang namespace
#endif
