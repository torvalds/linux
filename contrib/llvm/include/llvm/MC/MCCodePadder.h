//===- llvm/MC/CodePadder.h - MC Code Padder --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCCODEPADDER_H
#define LLVM_MC_MCCODEPADDER_H

#include "MCFragment.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {

class MCAsmLayout;
class MCCodePaddingPolicy;
class MCFragment;
class MCInst;
class MCObjectStreamer;
class MCSection;

typedef SmallVector<const MCPaddingFragment *, 8> MCPFRange;

struct MCCodePaddingContext {
  bool IsPaddingActive;
  bool IsBasicBlockReachableViaFallthrough;
  bool IsBasicBlockReachableViaBranch;
};

/// Target-independent base class incharge of all code padding decisions for a
/// target. During encoding it determines if and where MCPaddingFragments will
/// be located, as later on, when layout information is available, it determines
/// their sizes.
class MCCodePadder {
  MCCodePadder(const MCCodePadder &) = delete;
  void operator=(const MCCodePadder &) = delete;

  /// Determines if the MCCodePaddingPolicies are active.
  bool ArePoliciesActive;

  /// All the supported MCCodePaddingPolicies.
  SmallPtrSet<MCCodePaddingPolicy *, 4> CodePaddingPolicies;

  /// A pointer to the fragment of the instruction whose padding is currently
  /// done for.
  MCPaddingFragment *CurrHandledInstFragment;

  /// A map holding the jurisdiction for each padding fragment. Key: padding
  /// fragment. Value: The fragment's jurisdiction. A jurisdiction is a vector
  /// of padding fragments whose conditions are being controlled by another
  /// fragment, the key fragment.
  DenseMap<MCPaddingFragment *, MCPFRange> FragmentToJurisdiction;
  MCPFRange &getJurisdiction(MCPaddingFragment *Fragment, MCAsmLayout &Layout);

  /// A map holding the maximal instruction window size relevant for a padding
  /// fragment.
  DenseMap<MCPaddingFragment *, uint64_t> FragmentToMaxWindowSize;
  uint64_t getMaxWindowSize(MCPaddingFragment *Fragment, MCAsmLayout &Layout);

protected:
  /// The current streamer, used to stream code padding.
  MCObjectStreamer *OS;

  bool addPolicy(MCCodePaddingPolicy *Policy);

  virtual bool
  basicBlockRequiresInsertionPoint(const MCCodePaddingContext &Context) {
    return false;
  }

  virtual bool instructionRequiresInsertionPoint(const MCInst &Inst) {
    return false;
  }

  virtual bool usePoliciesForBasicBlock(const MCCodePaddingContext &Context) {
    return Context.IsPaddingActive;
  }

public:
  MCCodePadder()
      : ArePoliciesActive(false), CurrHandledInstFragment(nullptr),
        OS(nullptr) {}
  virtual ~MCCodePadder();

  /// Handles all target related code padding when starting to write a new
  /// basic block to an object file.
  ///
  /// \param OS The streamer used for writing the padding data and function.
  /// \param Context the context of the padding, Embeds the basic block's
  /// parameters.
  void handleBasicBlockStart(MCObjectStreamer *OS,
                             const MCCodePaddingContext &Context);
  /// Handles all target related code padding when done writing a block to an
  /// object file.
  ///
  /// \param Context the context of the padding, Embeds the basic block's
  /// parameters.
  void handleBasicBlockEnd(const MCCodePaddingContext &Context);
  /// Handles all target related code padding before writing a new instruction
  /// to an object file.
  ///
  /// \param Inst the instruction.
  void handleInstructionBegin(const MCInst &Inst);
  /// Handles all target related code padding after writing an instruction to an
  /// object file.
  ///
  /// \param Inst the instruction.
  void handleInstructionEnd(const MCInst &Inst);

  /// Relaxes a fragment (changes the size of the padding) according to target
  /// requirements. The new size computation is done w.r.t a layout.
  ///
  /// \param Fragment The fragment to relax.
  /// \param Layout Code layout information.
  ///
  /// \returns true iff any relaxation occurred.
  bool relaxFragment(MCPaddingFragment *Fragment, MCAsmLayout &Layout);
};

/// The base class for all padding policies, i.e. a rule or set of rules to pad
/// the generated code.
class MCCodePaddingPolicy {
  MCCodePaddingPolicy() = delete;
  MCCodePaddingPolicy(const MCCodePaddingPolicy &) = delete;
  void operator=(const MCCodePaddingPolicy &) = delete;

protected:
  /// A mask holding the kind of this policy, i.e. only the i'th bit will be set
  /// where i is the kind number.
  const uint64_t KindMask;
  /// Instruction window size relevant to this policy.
  const uint64_t WindowSize;
  /// A boolean indicating which byte of the instruction determies its
  /// instruction window. If true - the last byte of the instructions, o.w. -
  /// the first byte of the instruction.
  const bool InstByteIsLastByte;

  MCCodePaddingPolicy(uint64_t Kind, uint64_t WindowSize,
                      bool InstByteIsLastByte)
      : KindMask(UINT64_C(1) << Kind), WindowSize(WindowSize),
        InstByteIsLastByte(InstByteIsLastByte) {}

  /// Computes and returns the offset of the consecutive fragment of a given
  /// fragment.
  ///
  /// \param Fragment The fragment whose consecutive offset will be computed.
  /// \param Layout Code layout information.
  ///
  /// \returns the offset of the consecutive fragment of \p Fragment.
  static uint64_t getNextFragmentOffset(const MCFragment *Fragment,
                                        const MCAsmLayout &Layout);
  /// Returns the instruction byte of an instruction pointed by a given
  /// MCPaddingFragment. An instruction byte is the address of the byte of an
  /// instruction which determines its instruction window.
  ///
  /// \param Fragment The fragment pointing to the instruction.
  /// \param Layout Code layout information.
  ///
  /// \returns the instruction byte of an instruction pointed by \p Fragment.
  uint64_t getFragmentInstByte(const MCPaddingFragment *Fragment,
                               MCAsmLayout &Layout) const;
  uint64_t computeWindowEndAddress(const MCPaddingFragment *Fragment,
                                   uint64_t Offset, MCAsmLayout &Layout) const;

  /// Computes and returns the penalty weight of a first instruction window in a
  /// range. This requires a special function since the first window does not
  /// contain all the padding fragments in that window. It only contains all the
  /// padding fragments starting from the relevant insertion point.
  ///
  /// \param Window The first window.
  /// \param Offset The offset of the parent section relative to the beginning
  /// of the file, mod the window size.
  /// \param Layout Code layout information.
  ///
  /// \returns the penalty weight of a first instruction window in a range, \p
  /// Window.
  double computeFirstWindowPenaltyWeight(const MCPFRange &Window,
                                         uint64_t Offset,
                                         MCAsmLayout &Layout) const;
  /// Computes and returns the penalty caused by an instruction window.
  ///
  /// \param Window The instruction window.
  /// \param Offset The offset of the parent section relative to the beginning
  /// of the file, mod the window size.
  /// \param Layout Code layout information.
  ///
  /// \returns the penalty caused by \p Window.
  virtual double computeWindowPenaltyWeight(const MCPFRange &Window,
                                            uint64_t Offset,
                                            MCAsmLayout &Layout) const = 0;

public:
  virtual ~MCCodePaddingPolicy() {}

  /// Returns the kind mask of this policy -  A mask holding the kind of this
  /// policy, i.e. only the i'th bit will be set where i is the kind number.
  uint64_t getKindMask() const { return KindMask; }
  /// Returns the instruction window size relevant to this policy.
  uint64_t getWindowSize() const { return WindowSize; }
  /// Returns true if the last byte of an instruction determines its instruction
  /// window, or false if the first of an instruction determines it.
  bool isInstByteLastByte() const { return InstByteIsLastByte; }

  /// Returns true iff this policy needs padding for a given basic block.
  ///
  /// \param Context the context of the padding, Embeds the basic block's
  /// parameters.
  ///
  /// \returns true iff this policy needs padding for the basic block.
  virtual bool
  basicBlockRequiresPaddingFragment(const MCCodePaddingContext &Context) const {
    return false;
  }
  /// Returns true iff this policy needs padding for a given instruction.
  ///
  /// \param Inst The given instruction.
  ///
  /// \returns true iff this policy needs padding for \p Inst.
  virtual bool instructionRequiresPaddingFragment(const MCInst &Inst) const {
    return false;
  }
  /// Computes and returns the penalty caused by a range of instruction windows.
  /// The weight is computed for each window separelty and then accumulated.
  ///
  /// \param Range The range.
  /// \param Offset The offset of the parent section relative to the beginning
  /// of the file, mod the window size.
  /// \param Layout Code layout information.
  ///
  /// \returns the penalty caused by \p Range.
  double computeRangePenaltyWeight(const MCPFRange &Range, uint64_t Offset,
                                   MCAsmLayout &Layout) const;
};

} // namespace llvm

#endif // LLVM_MC_MCCODEPADDER_H
