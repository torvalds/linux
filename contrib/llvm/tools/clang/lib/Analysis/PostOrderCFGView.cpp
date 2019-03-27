//===- PostOrderCFGView.cpp - Post order view of CFG blocks ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements post order view of the blocks in a CFG.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/PostOrderCFGView.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFG.h"

using namespace clang;

void PostOrderCFGView::anchor() {}

PostOrderCFGView::PostOrderCFGView(const CFG *cfg) {
  Blocks.reserve(cfg->getNumBlockIDs());
  CFGBlockSet BSet(cfg);

  for (po_iterator I = po_iterator::begin(cfg, BSet),
                   E = po_iterator::end(cfg, BSet); I != E; ++I) {
    BlockOrder[*I] = Blocks.size() + 1;
    Blocks.push_back(*I);
  }
}

PostOrderCFGView *PostOrderCFGView::create(AnalysisDeclContext &ctx) {
  const CFG *cfg = ctx.getCFG();
  if (!cfg)
    return nullptr;
  return new PostOrderCFGView(cfg);
}

const void *PostOrderCFGView::getTag() { static int x; return &x; }

bool PostOrderCFGView::BlockOrderCompare::operator()(const CFGBlock *b1,
                                                     const CFGBlock *b2) const {
  PostOrderCFGView::BlockOrderTy::const_iterator b1It = POV.BlockOrder.find(b1);
  PostOrderCFGView::BlockOrderTy::const_iterator b2It = POV.BlockOrder.find(b2);

  unsigned b1V = (b1It == POV.BlockOrder.end()) ? 0 : b1It->second;
  unsigned b2V = (b2It == POV.BlockOrder.end()) ? 0 : b2It->second;
  return b1V > b2V;
}
