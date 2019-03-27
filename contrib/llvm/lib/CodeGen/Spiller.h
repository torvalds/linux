//===- llvm/CodeGen/Spiller.h - Spiller -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_SPILLER_H
#define LLVM_LIB_CODEGEN_SPILLER_H

namespace llvm {

class LiveRangeEdit;
class MachineFunction;
class MachineFunctionPass;
class VirtRegMap;

  /// Spiller interface.
  ///
  /// Implementations are utility classes which insert spill or remat code on
  /// demand.
  class Spiller {
    virtual void anchor();

  public:
    virtual ~Spiller() = 0;

    /// spill - Spill the LRE.getParent() live interval.
    virtual void spill(LiveRangeEdit &LRE) = 0;

    virtual void postOptimization() {}
  };

  /// Create and return a spiller that will insert spill code directly instead
  /// of deferring though VirtRegMap.
  Spiller *createInlineSpiller(MachineFunctionPass &pass,
                               MachineFunction &mf,
                               VirtRegMap &vrm);

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_SPILLER_H
