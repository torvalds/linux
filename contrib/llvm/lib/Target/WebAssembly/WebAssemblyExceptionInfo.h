//===-- WebAssemblyExceptionInfo.h - WebAssembly Exception Info -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief This file implements WebAssemblyException information analysis.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYEXCEPTIONINFO_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYEXCEPTIONINFO_H

#include "WebAssembly.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class MachineDominatorTree;
class MachineDominanceFrontier;

// WebAssembly instructions for exception handling are structured as follows:
//   try
//     instructions*
//   catch             ----|
//     instructions*       | -> A WebAssemblyException consists of this region
//   end               ----|
//
// A WebAssemblyException object contains BBs that belong to a 'catch' part of
// the try-catch-end structure to be created later. 'try' and 'end' markers
// are not present at this stage and will be generated in CFGStackify pass.
// Because CFGSort requires all the BBs within a catch part to be sorted
// together as it does for loops, this pass calculates the nesting structure of
// catch part of exceptions in a function.
//
// An exception catch part is defined as a BB with catch instruction and all
// other BBs dominated by this BB.
class WebAssemblyException {
  MachineBasicBlock *EHPad = nullptr;

  WebAssemblyException *ParentException = nullptr;
  std::vector<WebAssemblyException *> SubExceptions;
  std::vector<MachineBasicBlock *> Blocks;
  SmallPtrSet<const MachineBasicBlock *, 8> BlockSet;

public:
  WebAssemblyException(MachineBasicBlock *EHPad) : EHPad(EHPad) {}
  ~WebAssemblyException() { DeleteContainerPointers(SubExceptions); }
  WebAssemblyException(const WebAssemblyException &) = delete;
  const WebAssemblyException &operator=(const WebAssemblyException &) = delete;

  MachineBasicBlock *getEHPad() const { return EHPad; }
  MachineBasicBlock *getHeader() const { return EHPad; }
  WebAssemblyException *getParentException() const { return ParentException; }
  void setParentException(WebAssemblyException *WE) { ParentException = WE; }

  bool contains(const WebAssemblyException *WE) const {
    if (WE == this)
      return true;
    if (!WE)
      return false;
    return contains(WE->getParentException());
  }
  bool contains(const MachineBasicBlock *MBB) const {
    return BlockSet.count(MBB);
  }

  void addBlock(MachineBasicBlock *MBB) {
    Blocks.push_back(MBB);
    BlockSet.insert(MBB);
  }
  ArrayRef<MachineBasicBlock *> getBlocks() const { return Blocks; }
  using block_iterator = typename ArrayRef<MachineBasicBlock *>::const_iterator;
  block_iterator block_begin() const { return getBlocks().begin(); }
  block_iterator block_end() const { return getBlocks().end(); }
  inline iterator_range<block_iterator> blocks() const {
    return make_range(block_begin(), block_end());
  }
  unsigned getNumBlocks() const { return Blocks.size(); }
  std::vector<MachineBasicBlock *> &getBlocksVector() { return Blocks; }

  const std::vector<WebAssemblyException *> &getSubExceptions() const {
    return SubExceptions;
  }
  std::vector<WebAssemblyException *> &getSubExceptions() {
    return SubExceptions;
  }
  void addSubException(WebAssemblyException *E) { SubExceptions.push_back(E); }
  using iterator = typename std::vector<WebAssemblyException *>::const_iterator;
  iterator begin() const { return SubExceptions.begin(); }
  iterator end() const { return SubExceptions.end(); }

  void reserveBlocks(unsigned Size) { Blocks.reserve(Size); }
  void reverseBlock(unsigned From = 0) {
    std::reverse(Blocks.begin() + From, Blocks.end());
  }

  // Return the nesting level. An outermost one has depth 1.
  unsigned getExceptionDepth() const {
    unsigned D = 1;
    for (const WebAssemblyException *CurException = ParentException;
         CurException; CurException = CurException->ParentException)
      ++D;
    return D;
  }

  void print(raw_ostream &OS, unsigned Depth = 0) const;
  void dump() const;
};

raw_ostream &operator<<(raw_ostream &OS, const WebAssemblyException &WE);

class WebAssemblyExceptionInfo final : public MachineFunctionPass {
  // Mapping of basic blocks to the innermost exception they occur in
  DenseMap<const MachineBasicBlock *, WebAssemblyException *> BBMap;
  std::vector<WebAssemblyException *> TopLevelExceptions;

  void discoverAndMapException(WebAssemblyException *WE,
                               const MachineDominatorTree &MDT,
                               const MachineDominanceFrontier &MDF);
  WebAssemblyException *getOutermostException(MachineBasicBlock *MBB) const;

public:
  static char ID;
  WebAssemblyExceptionInfo() : MachineFunctionPass(ID) {
    initializeWebAssemblyExceptionInfoPass(*PassRegistry::getPassRegistry());
  }
  ~WebAssemblyExceptionInfo() override { releaseMemory(); }
  WebAssemblyExceptionInfo(const WebAssemblyExceptionInfo &) = delete;
  WebAssemblyExceptionInfo &
  operator=(const WebAssemblyExceptionInfo &) = delete;

  bool runOnMachineFunction(MachineFunction &) override;
  void releaseMemory() override;
  void recalculate(MachineDominatorTree &MDT,
                   const MachineDominanceFrontier &MDF);
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  bool empty() const { return TopLevelExceptions.empty(); }

  // Return the innermost exception that MBB lives in. If the block is not in an
  // exception, null is returned.
  WebAssemblyException *getExceptionFor(const MachineBasicBlock *MBB) const {
    return BBMap.lookup(MBB);
  }

  void changeExceptionFor(MachineBasicBlock *MBB, WebAssemblyException *WE) {
    if (!WE) {
      BBMap.erase(MBB);
      return;
    }
    BBMap[MBB] = WE;
  }

  void addTopLevelException(WebAssemblyException *WE) {
    assert(!WE->getParentException() && "Not a top level exception!");
    TopLevelExceptions.push_back(WE);
  }

  void print(raw_ostream &OS, const Module *M = nullptr) const override;
};

} // end namespace llvm

#endif
