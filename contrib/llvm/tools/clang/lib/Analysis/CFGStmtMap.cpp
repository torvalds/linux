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

#include "llvm/ADT/DenseMap.h"
#include "clang/AST/ParentMap.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/CFGStmtMap.h"

using namespace clang;

typedef llvm::DenseMap<const Stmt*, CFGBlock*> SMap;
static SMap *AsMap(void *m) { return (SMap*) m; }

CFGStmtMap::~CFGStmtMap() { delete AsMap(M); }

CFGBlock *CFGStmtMap::getBlock(Stmt *S) {
  SMap *SM = AsMap(M);
  Stmt *X = S;

  // If 'S' isn't in the map, walk the ParentMap to see if one of its ancestors
  // is in the map.
  while (X) {
    SMap::iterator I = SM->find(X);
    if (I != SM->end()) {
      CFGBlock *B = I->second;
      // Memoize this lookup.
      if (X != S)
        (*SM)[X] = B;
      return B;
    }

    X = PM->getParentIgnoreParens(X);
  }

  return nullptr;
}

static void Accumulate(SMap &SM, CFGBlock *B) {
  // First walk the block-level expressions.
  for (CFGBlock::iterator I = B->begin(), E = B->end(); I != E; ++I) {
    const CFGElement &CE = *I;
    Optional<CFGStmt> CS = CE.getAs<CFGStmt>();
    if (!CS)
      continue;

    CFGBlock *&Entry = SM[CS->getStmt()];
    // If 'Entry' is already initialized (e.g., a terminator was already),
    // skip.
    if (Entry)
      continue;

    Entry = B;

  }

  // Look at the label of the block.
  if (Stmt *Label = B->getLabel())
    SM[Label] = B;

  // Finally, look at the terminator.  If the terminator was already added
  // because it is a block-level expression in another block, overwrite
  // that mapping.
  if (Stmt *Term = B->getTerminator())
    SM[Term] = B;
}

CFGStmtMap *CFGStmtMap::Build(CFG *C, ParentMap *PM) {
  if (!C || !PM)
    return nullptr;

  SMap *SM = new SMap();

  // Walk all blocks, accumulating the block-level expressions, labels,
  // and terminators.
  for (CFG::iterator I = C->begin(), E = C->end(); I != E; ++I)
    Accumulate(*SM, *I);

  return new CFGStmtMap(PM, SM);
}

