//===----- llvm/Analysis/CaptureTracking.h - Pointer capture ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains routines that help determine which pointers are captured.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CAPTURETRACKING_H
#define LLVM_ANALYSIS_CAPTURETRACKING_H

namespace llvm {

  class Value;
  class Use;
  class Instruction;
  class DominatorTree;
  class OrderedBasicBlock;

  /// The default value for MaxUsesToExplore argument. It's relatively small to
  /// keep the cost of analysis reasonable for clients like BasicAliasAnalysis,
  /// where the results can't be cached.
  /// TODO: we should probably introduce a caching CaptureTracking analysis and
  /// use it where possible. The caching version can use much higher limit or
  /// don't have this cap at all.
  unsigned constexpr DefaultMaxUsesToExplore = 20;

  /// PointerMayBeCaptured - Return true if this pointer value may be captured
  /// by the enclosing function (which is required to exist).  This routine can
  /// be expensive, so consider caching the results.  The boolean ReturnCaptures
  /// specifies whether returning the value (or part of it) from the function
  /// counts as capturing it or not.  The boolean StoreCaptures specified
  /// whether storing the value (or part of it) into memory anywhere
  /// automatically counts as capturing it or not.
  /// MaxUsesToExplore specifies how many uses should the analysis explore for
  /// one value before giving up due too "too many uses".
  bool PointerMayBeCaptured(const Value *V,
                            bool ReturnCaptures,
                            bool StoreCaptures,
                            unsigned MaxUsesToExplore = DefaultMaxUsesToExplore);

  /// PointerMayBeCapturedBefore - Return true if this pointer value may be
  /// captured by the enclosing function (which is required to exist). If a
  /// DominatorTree is provided, only captures which happen before the given
  /// instruction are considered. This routine can be expensive, so consider
  /// caching the results.  The boolean ReturnCaptures specifies whether
  /// returning the value (or part of it) from the function counts as capturing
  /// it or not.  The boolean StoreCaptures specified whether storing the value
  /// (or part of it) into memory anywhere automatically counts as capturing it
  /// or not. Captures by the provided instruction are considered if the
  /// final parameter is true. An ordered basic block in \p OBB could be used
  /// to speed up capture-tracker queries.
  /// MaxUsesToExplore specifies how many uses should the analysis explore for
  /// one value before giving up due too "too many uses".
  bool PointerMayBeCapturedBefore(const Value *V, bool ReturnCaptures,
                                  bool StoreCaptures, const Instruction *I,
                                  const DominatorTree *DT, bool IncludeI = false,
                                  OrderedBasicBlock *OBB = nullptr,
                                  unsigned MaxUsesToExplore = DefaultMaxUsesToExplore);

  /// This callback is used in conjunction with PointerMayBeCaptured. In
  /// addition to the interface here, you'll need to provide your own getters
  /// to see whether anything was captured.
  struct CaptureTracker {
    virtual ~CaptureTracker();

    /// tooManyUses - The depth of traversal has breached a limit. There may be
    /// capturing instructions that will not be passed into captured().
    virtual void tooManyUses() = 0;

    /// shouldExplore - This is the use of a value derived from the pointer.
    /// To prune the search (ie., assume that none of its users could possibly
    /// capture) return false. To search it, return true.
    ///
    /// U->getUser() is always an Instruction.
    virtual bool shouldExplore(const Use *U);

    /// captured - Information about the pointer was captured by the user of
    /// use U. Return true to stop the traversal or false to continue looking
    /// for more capturing instructions.
    virtual bool captured(const Use *U) = 0;
  };

  /// PointerMayBeCaptured - Visit the value and the values derived from it and
  /// find values which appear to be capturing the pointer value. This feeds
  /// results into and is controlled by the CaptureTracker object.
  /// MaxUsesToExplore specifies how many uses should the analysis explore for
  /// one value before giving up due too "too many uses".
  void PointerMayBeCaptured(const Value *V, CaptureTracker *Tracker,
                            unsigned MaxUsesToExplore = DefaultMaxUsesToExplore);
} // end namespace llvm

#endif
