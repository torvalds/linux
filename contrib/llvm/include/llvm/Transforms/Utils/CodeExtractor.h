//===- Transform/Utils/CodeExtractor.h - Code extraction util ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// A utility to support extracting code from one function into its own
// stand-alone function.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_CODEEXTRACTOR_H
#define LLVM_TRANSFORMS_UTILS_CODEEXTRACTOR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include <limits>

namespace llvm {

class BasicBlock;
class BlockFrequency;
class BlockFrequencyInfo;
class BranchProbabilityInfo;
class CallInst;
class DominatorTree;
class Function;
class Instruction;
class Loop;
class Module;
class Type;
class Value;

  /// Utility class for extracting code into a new function.
  ///
  /// This utility provides a simple interface for extracting some sequence of
  /// code into its own function, replacing it with a call to that function. It
  /// also provides various methods to query about the nature and result of
  /// such a transformation.
  ///
  /// The rough algorithm used is:
  /// 1) Find both the inputs and outputs for the extracted region.
  /// 2) Pass the inputs as arguments, remapping them within the extracted
  ///    function to arguments.
  /// 3) Add allocas for any scalar outputs, adding all of the outputs' allocas
  ///    as arguments, and inserting stores to the arguments for any scalars.
  class CodeExtractor {
    using ValueSet = SetVector<Value *>;

    // Various bits of state computed on construction.
    DominatorTree *const DT;
    const bool AggregateArgs;
    BlockFrequencyInfo *BFI;
    BranchProbabilityInfo *BPI;

    // If true, varargs functions can be extracted.
    bool AllowVarArgs;

    // Bits of intermediate state computed at various phases of extraction.
    SetVector<BasicBlock *> Blocks;
    unsigned NumExitBlocks = std::numeric_limits<unsigned>::max();
    Type *RetTy;

    // Suffix to use when creating extracted function (appended to the original
    // function name + "."). If empty, the default is to use the entry block
    // label, if non-empty, otherwise "extracted".
    std::string Suffix;

  public:
    /// Create a code extractor for a sequence of blocks.
    ///
    /// Given a sequence of basic blocks where the first block in the sequence
    /// dominates the rest, prepare a code extractor object for pulling this
    /// sequence out into its new function. When a DominatorTree is also given,
    /// extra checking and transformations are enabled. If AllowVarArgs is true,
    /// vararg functions can be extracted. This is safe, if all vararg handling
    /// code is extracted, including vastart. If AllowAlloca is true, then
    /// extraction of blocks containing alloca instructions would be possible,
    /// however code extractor won't validate whether extraction is legal.
    CodeExtractor(ArrayRef<BasicBlock *> BBs, DominatorTree *DT = nullptr,
                  bool AggregateArgs = false, BlockFrequencyInfo *BFI = nullptr,
                  BranchProbabilityInfo *BPI = nullptr,
                  bool AllowVarArgs = false, bool AllowAlloca = false,
                  std::string Suffix = "");

    /// Create a code extractor for a loop body.
    ///
    /// Behaves just like the generic code sequence constructor, but uses the
    /// block sequence of the loop.
    CodeExtractor(DominatorTree &DT, Loop &L, bool AggregateArgs = false,
                  BlockFrequencyInfo *BFI = nullptr,
                  BranchProbabilityInfo *BPI = nullptr,
                  std::string Suffix = "");

    /// Perform the extraction, returning the new function.
    ///
    /// Returns zero when called on a CodeExtractor instance where isEligible
    /// returns false.
    Function *extractCodeRegion();

    /// Test whether this code extractor is eligible.
    ///
    /// Based on the blocks used when constructing the code extractor,
    /// determine whether it is eligible for extraction.
    bool isEligible() const { return !Blocks.empty(); }

    /// Compute the set of input values and output values for the code.
    ///
    /// These can be used either when performing the extraction or to evaluate
    /// the expected size of a call to the extracted function. Note that this
    /// work cannot be cached between the two as once we decide to extract
    /// a code sequence, that sequence is modified, including changing these
    /// sets, before extraction occurs. These modifications won't have any
    /// significant impact on the cost however.
    void findInputsOutputs(ValueSet &Inputs, ValueSet &Outputs,
                           const ValueSet &Allocas) const;

    /// Check if life time marker nodes can be hoisted/sunk into the outline
    /// region.
    ///
    /// Returns true if it is safe to do the code motion.
    bool isLegalToShrinkwrapLifetimeMarkers(Instruction *AllocaAddr) const;

    /// Find the set of allocas whose life ranges are contained within the
    /// outlined region.
    ///
    /// Allocas which have life_time markers contained in the outlined region
    /// should be pushed to the outlined function. The address bitcasts that
    /// are used by the lifetime markers are also candidates for shrink-
    /// wrapping. The instructions that need to be sunk are collected in
    /// 'Allocas'.
    void findAllocas(ValueSet &SinkCands, ValueSet &HoistCands,
                     BasicBlock *&ExitBlock) const;

    /// Find or create a block within the outline region for placing hoisted
    /// code.
    ///
    /// CommonExitBlock is block outside the outline region. It is the common
    /// successor of blocks inside the region. If there exists a single block
    /// inside the region that is the predecessor of CommonExitBlock, that block
    /// will be returned. Otherwise CommonExitBlock will be split and the
    /// original block will be added to the outline region.
    BasicBlock *findOrCreateBlockForHoisting(BasicBlock *CommonExitBlock);

  private:
    void severSplitPHINodesOfEntry(BasicBlock *&Header);
    void severSplitPHINodesOfExits(const SmallPtrSetImpl<BasicBlock *> &Exits);
    void splitReturnBlocks();

    Function *constructFunction(const ValueSet &inputs,
                                const ValueSet &outputs,
                                BasicBlock *header,
                                BasicBlock *newRootNode, BasicBlock *newHeader,
                                Function *oldFunction, Module *M);

    void moveCodeToFunction(Function *newFunction);

    void calculateNewCallTerminatorWeights(
        BasicBlock *CodeReplacer,
        DenseMap<BasicBlock *, BlockFrequency> &ExitWeights,
        BranchProbabilityInfo *BPI);

    CallInst *emitCallAndSwitchStatement(Function *newFunction,
                                         BasicBlock *newHeader,
                                         ValueSet &inputs, ValueSet &outputs);
  };

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_CODEEXTRACTOR_H
