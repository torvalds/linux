//===- BlockExtractor.cpp - Extracts blocks into their own functions ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass extracts the specified basic blocks from the module into their
// own functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
using namespace llvm;

#define DEBUG_TYPE "block-extractor"

STATISTIC(NumExtracted, "Number of basic blocks extracted");

static cl::opt<std::string> BlockExtractorFile(
    "extract-blocks-file", cl::value_desc("filename"),
    cl::desc("A file containing list of basic blocks to extract"), cl::Hidden);

cl::opt<bool> BlockExtractorEraseFuncs("extract-blocks-erase-funcs",
                                       cl::desc("Erase the existing functions"),
                                       cl::Hidden);

namespace {
class BlockExtractor : public ModulePass {
  SmallVector<BasicBlock *, 16> Blocks;
  bool EraseFunctions;
  SmallVector<std::pair<std::string, std::string>, 32> BlocksByName;

public:
  static char ID;
  BlockExtractor(const SmallVectorImpl<BasicBlock *> &BlocksToExtract,
                 bool EraseFunctions)
      : ModulePass(ID), Blocks(BlocksToExtract.begin(), BlocksToExtract.end()),
        EraseFunctions(EraseFunctions) {
    if (!BlockExtractorFile.empty())
      loadFile();
  }
  BlockExtractor() : BlockExtractor(SmallVector<BasicBlock *, 0>(), false) {}
  bool runOnModule(Module &M) override;

private:
  void loadFile();
  void splitLandingPadPreds(Function &F);
};
} // end anonymous namespace

char BlockExtractor::ID = 0;
INITIALIZE_PASS(BlockExtractor, "extract-blocks",
                "Extract basic blocks from module", false, false)

ModulePass *llvm::createBlockExtractorPass() { return new BlockExtractor(); }
ModulePass *llvm::createBlockExtractorPass(
    const SmallVectorImpl<BasicBlock *> &BlocksToExtract, bool EraseFunctions) {
  return new BlockExtractor(BlocksToExtract, EraseFunctions);
}

/// Gets all of the blocks specified in the input file.
void BlockExtractor::loadFile() {
  auto ErrOrBuf = MemoryBuffer::getFile(BlockExtractorFile);
  if (ErrOrBuf.getError())
    report_fatal_error("BlockExtractor couldn't load the file.");
  // Read the file.
  auto &Buf = *ErrOrBuf;
  SmallVector<StringRef, 16> Lines;
  Buf->getBuffer().split(Lines, '\n', /*MaxSplit=*/-1,
                         /*KeepEmpty=*/false);
  for (const auto &Line : Lines) {
    auto FBPair = Line.split(' ');
    BlocksByName.push_back({FBPair.first, FBPair.second});
  }
}

/// Extracts the landing pads to make sure all of them have only one
/// predecessor.
void BlockExtractor::splitLandingPadPreds(Function &F) {
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      if (!isa<InvokeInst>(&I))
        continue;
      InvokeInst *II = cast<InvokeInst>(&I);
      BasicBlock *Parent = II->getParent();
      BasicBlock *LPad = II->getUnwindDest();

      // Look through the landing pad's predecessors. If one of them ends in an
      // 'invoke', then we want to split the landing pad.
      bool Split = false;
      for (auto PredBB : predecessors(LPad)) {
        if (PredBB->isLandingPad() && PredBB != Parent &&
            isa<InvokeInst>(Parent->getTerminator())) {
          Split = true;
          break;
        }
      }

      if (!Split)
        continue;

      SmallVector<BasicBlock *, 2> NewBBs;
      SplitLandingPadPredecessors(LPad, Parent, ".1", ".2", NewBBs);
    }
  }
}

bool BlockExtractor::runOnModule(Module &M) {

  bool Changed = false;

  // Get all the functions.
  SmallVector<Function *, 4> Functions;
  for (Function &F : M) {
    splitLandingPadPreds(F);
    Functions.push_back(&F);
  }

  // Get all the blocks specified in the input file.
  for (const auto &BInfo : BlocksByName) {
    Function *F = M.getFunction(BInfo.first);
    if (!F)
      report_fatal_error("Invalid function name specified in the input file");
    auto Res = llvm::find_if(*F, [&](const BasicBlock &BB) {
      return BB.getName().equals(BInfo.second);
    });
    if (Res == F->end())
      report_fatal_error("Invalid block name specified in the input file");
    Blocks.push_back(&*Res);
  }

  // Extract basic blocks.
  for (BasicBlock *BB : Blocks) {
    // Check if the module contains BB.
    if (BB->getParent()->getParent() != &M)
      report_fatal_error("Invalid basic block");
    LLVM_DEBUG(dbgs() << "BlockExtractor: Extracting "
                      << BB->getParent()->getName() << ":" << BB->getName()
                      << "\n");
    SmallVector<BasicBlock *, 2> BlocksToExtractVec;
    BlocksToExtractVec.push_back(BB);
    if (const InvokeInst *II = dyn_cast<InvokeInst>(BB->getTerminator()))
      BlocksToExtractVec.push_back(II->getUnwindDest());
    CodeExtractor(BlocksToExtractVec).extractCodeRegion();
    ++NumExtracted;
    Changed = true;
  }

  // Erase the functions.
  if (EraseFunctions || BlockExtractorEraseFuncs) {
    for (Function *F : Functions) {
      LLVM_DEBUG(dbgs() << "BlockExtractor: Trying to delete " << F->getName()
                        << "\n");
      F->deleteBody();
    }
    // Set linkage as ExternalLinkage to avoid erasing unreachable functions.
    for (Function &F : M)
      F.setLinkage(GlobalValue::ExternalLinkage);
    Changed = true;
  }

  return Changed;
}
