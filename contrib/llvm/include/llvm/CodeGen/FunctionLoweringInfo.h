//===- FunctionLoweringInfo.h - Lower functions from LLVM IR ---*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This implements routines for translating functions from LLVM IR into
// Machine IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_FUNCTIONLOWERINGINFO_H
#define LLVM_CODEGEN_FUNCTIONLOWERINGINFO_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/KnownBits.h"
#include <cassert>
#include <utility>
#include <vector>

namespace llvm {

class Argument;
class BasicBlock;
class BranchProbabilityInfo;
class Function;
class Instruction;
class MachineFunction;
class MachineInstr;
class MachineRegisterInfo;
class MVT;
class SelectionDAG;
class TargetLowering;

//===--------------------------------------------------------------------===//
/// FunctionLoweringInfo - This contains information that is global to a
/// function that is used when lowering a region of the function.
///
class FunctionLoweringInfo {
public:
  const Function *Fn;
  MachineFunction *MF;
  const TargetLowering *TLI;
  MachineRegisterInfo *RegInfo;
  BranchProbabilityInfo *BPI;
  /// CanLowerReturn - true iff the function's return value can be lowered to
  /// registers.
  bool CanLowerReturn;

  /// True if part of the CSRs will be handled via explicit copies.
  bool SplitCSR;

  /// DemoteRegister - if CanLowerReturn is false, DemoteRegister is a vreg
  /// allocated to hold a pointer to the hidden sret parameter.
  unsigned DemoteRegister;

  /// MBBMap - A mapping from LLVM basic blocks to their machine code entry.
  DenseMap<const BasicBlock*, MachineBasicBlock *> MBBMap;

  /// A map from swifterror value in a basic block to the virtual register it is
  /// currently represented by.
  DenseMap<std::pair<const MachineBasicBlock *, const Value *>, unsigned>
      SwiftErrorVRegDefMap;

  /// A list of upward exposed vreg uses that need to be satisfied by either a
  /// copy def or a phi node at the beginning of the basic block representing
  /// the predecessor(s) swifterror value.
  DenseMap<std::pair<const MachineBasicBlock *, const Value *>, unsigned>
      SwiftErrorVRegUpwardsUse;

  /// A map from instructions that define/use a swifterror value to the virtual
  /// register that represents that def/use.
  llvm::DenseMap<PointerIntPair<const Instruction *, 1, bool>, unsigned>
      SwiftErrorVRegDefUses;

  /// The swifterror argument of the current function.
  const Value *SwiftErrorArg;

  using SwiftErrorValues = SmallVector<const Value*, 1>;
  /// A function can only have a single swifterror argument. And if it does
  /// have a swifterror argument, it must be the first entry in
  /// SwiftErrorVals.
  SwiftErrorValues SwiftErrorVals;

  /// Get or create the swifterror value virtual register in
  /// SwiftErrorVRegDefMap for this basic block.
  unsigned getOrCreateSwiftErrorVReg(const MachineBasicBlock *,
                                     const Value *);

  /// Set the swifterror virtual register in the SwiftErrorVRegDefMap for this
  /// basic block.
  void setCurrentSwiftErrorVReg(const MachineBasicBlock *MBB, const Value *,
                                unsigned);

  /// Get or create the swifterror value virtual register for a def of a
  /// swifterror by an instruction.
  std::pair<unsigned, bool> getOrCreateSwiftErrorVRegDefAt(const Instruction *);
  std::pair<unsigned, bool>
  getOrCreateSwiftErrorVRegUseAt(const Instruction *, const MachineBasicBlock *,
                                 const Value *);

  /// ValueMap - Since we emit code for the function a basic block at a time,
  /// we must remember which virtual registers hold the values for
  /// cross-basic-block values.
  DenseMap<const Value *, unsigned> ValueMap;

  /// VirtReg2Value map is needed by the Divergence Analysis driven
  /// instruction selection. It is reverted ValueMap. It is computed
  /// in lazy style - on demand. It is used to get the Value corresponding
  /// to the live in virtual register and is called from the
  /// TargetLowerinInfo::isSDNodeSourceOfDivergence.
  DenseMap<unsigned, const Value*> VirtReg2Value;

  /// This method is called from TargetLowerinInfo::isSDNodeSourceOfDivergence
  /// to get the Value corresponding to the live-in virtual register.
  const Value * getValueFromVirtualReg(unsigned Vreg);

  /// Track virtual registers created for exception pointers.
  DenseMap<const Value *, unsigned> CatchPadExceptionPointers;

  /// Keep track of frame indices allocated for statepoints as they could be
  /// used across basic block boundaries.  This struct is more complex than a
  /// simple map because the stateopint lowering code de-duplicates gc pointers
  /// based on their SDValue (so %p and (bitcast %p to T) will get the same
  /// slot), and we track that here.

  struct StatepointSpillMap {
    using SlotMapTy = DenseMap<const Value *, Optional<int>>;

    /// Maps uniqued llvm IR values to the slots they were spilled in.  If a
    /// value is mapped to None it means we visited the value but didn't spill
    /// it (because it was a constant, for instance).
    SlotMapTy SlotMap;

    /// Maps llvm IR values to the values they were de-duplicated to.
    DenseMap<const Value *, const Value *> DuplicateMap;

    SlotMapTy::const_iterator find(const Value *V) const {
      auto DuplIt = DuplicateMap.find(V);
      if (DuplIt != DuplicateMap.end())
        V = DuplIt->second;
      return SlotMap.find(V);
    }

    SlotMapTy::const_iterator end() const { return SlotMap.end(); }
  };

  /// Maps gc.statepoint instructions to their corresponding StatepointSpillMap
  /// instances.
  DenseMap<const Instruction *, StatepointSpillMap> StatepointSpillMaps;

  /// StaticAllocaMap - Keep track of frame indices for fixed sized allocas in
  /// the entry block.  This allows the allocas to be efficiently referenced
  /// anywhere in the function.
  DenseMap<const AllocaInst*, int> StaticAllocaMap;

  /// ByValArgFrameIndexMap - Keep track of frame indices for byval arguments.
  DenseMap<const Argument*, int> ByValArgFrameIndexMap;

  /// ArgDbgValues - A list of DBG_VALUE instructions created during isel for
  /// function arguments that are inserted after scheduling is completed.
  SmallVector<MachineInstr*, 8> ArgDbgValues;

  /// RegFixups - Registers which need to be replaced after isel is done.
  DenseMap<unsigned, unsigned> RegFixups;

  DenseSet<unsigned> RegsWithFixups;

  /// StatepointStackSlots - A list of temporary stack slots (frame indices)
  /// used to spill values at a statepoint.  We store them here to enable
  /// reuse of the same stack slots across different statepoints in different
  /// basic blocks.
  SmallVector<unsigned, 50> StatepointStackSlots;

  /// MBB - The current block.
  MachineBasicBlock *MBB;

  /// MBB - The current insert position inside the current block.
  MachineBasicBlock::iterator InsertPt;

  struct LiveOutInfo {
    unsigned NumSignBits : 31;
    unsigned IsValid : 1;
    KnownBits Known = 1;

    LiveOutInfo() : NumSignBits(0), IsValid(true) {}
  };

  /// Record the preferred extend type (ISD::SIGN_EXTEND or ISD::ZERO_EXTEND)
  /// for a value.
  DenseMap<const Value *, ISD::NodeType> PreferredExtendType;

  /// VisitedBBs - The set of basic blocks visited thus far by instruction
  /// selection.
  SmallPtrSet<const BasicBlock*, 4> VisitedBBs;

  /// PHINodesToUpdate - A list of phi instructions whose operand list will
  /// be updated after processing the current basic block.
  /// TODO: This isn't per-function state, it's per-basic-block state. But
  /// there's no other convenient place for it to live right now.
  std::vector<std::pair<MachineInstr*, unsigned> > PHINodesToUpdate;
  unsigned OrigNumPHINodesToUpdate;

  /// If the current MBB is a landing pad, the exception pointer and exception
  /// selector registers are copied into these virtual registers by
  /// SelectionDAGISel::PrepareEHLandingPad().
  unsigned ExceptionPointerVirtReg, ExceptionSelectorVirtReg;

  /// set - Initialize this FunctionLoweringInfo with the given Function
  /// and its associated MachineFunction.
  ///
  void set(const Function &Fn, MachineFunction &MF, SelectionDAG *DAG);

  /// clear - Clear out all the function-specific state. This returns this
  /// FunctionLoweringInfo to an empty state, ready to be used for a
  /// different function.
  void clear();

  /// isExportedInst - Return true if the specified value is an instruction
  /// exported from its block.
  bool isExportedInst(const Value *V) {
    return ValueMap.count(V);
  }

  unsigned CreateReg(MVT VT);

  unsigned CreateRegs(Type *Ty);

  unsigned InitializeRegForValue(const Value *V) {
    // Tokens never live in vregs.
    if (V->getType()->isTokenTy())
      return 0;
    unsigned &R = ValueMap[V];
    assert(R == 0 && "Already initialized this value register!");
    assert(VirtReg2Value.empty());
    return R = CreateRegs(V->getType());
  }

  /// GetLiveOutRegInfo - Gets LiveOutInfo for a register, returning NULL if the
  /// register is a PHI destination and the PHI's LiveOutInfo is not valid.
  const LiveOutInfo *GetLiveOutRegInfo(unsigned Reg) {
    if (!LiveOutRegInfo.inBounds(Reg))
      return nullptr;

    const LiveOutInfo *LOI = &LiveOutRegInfo[Reg];
    if (!LOI->IsValid)
      return nullptr;

    return LOI;
  }

  /// GetLiveOutRegInfo - Gets LiveOutInfo for a register, returning NULL if the
  /// register is a PHI destination and the PHI's LiveOutInfo is not valid. If
  /// the register's LiveOutInfo is for a smaller bit width, it is extended to
  /// the larger bit width by zero extension. The bit width must be no smaller
  /// than the LiveOutInfo's existing bit width.
  const LiveOutInfo *GetLiveOutRegInfo(unsigned Reg, unsigned BitWidth);

  /// AddLiveOutRegInfo - Adds LiveOutInfo for a register.
  void AddLiveOutRegInfo(unsigned Reg, unsigned NumSignBits,
                         const KnownBits &Known) {
    // Only install this information if it tells us something.
    if (NumSignBits == 1 && Known.isUnknown())
      return;

    LiveOutRegInfo.grow(Reg);
    LiveOutInfo &LOI = LiveOutRegInfo[Reg];
    LOI.NumSignBits = NumSignBits;
    LOI.Known.One = Known.One;
    LOI.Known.Zero = Known.Zero;
  }

  /// ComputePHILiveOutRegInfo - Compute LiveOutInfo for a PHI's destination
  /// register based on the LiveOutInfo of its operands.
  void ComputePHILiveOutRegInfo(const PHINode*);

  /// InvalidatePHILiveOutRegInfo - Invalidates a PHI's LiveOutInfo, to be
  /// called when a block is visited before all of its predecessors.
  void InvalidatePHILiveOutRegInfo(const PHINode *PN) {
    // PHIs with no uses have no ValueMap entry.
    DenseMap<const Value*, unsigned>::const_iterator It = ValueMap.find(PN);
    if (It == ValueMap.end())
      return;

    unsigned Reg = It->second;
    if (Reg == 0)
      return;

    LiveOutRegInfo.grow(Reg);
    LiveOutRegInfo[Reg].IsValid = false;
  }

  /// setArgumentFrameIndex - Record frame index for the byval
  /// argument.
  void setArgumentFrameIndex(const Argument *A, int FI);

  /// getArgumentFrameIndex - Get frame index for the byval argument.
  int getArgumentFrameIndex(const Argument *A);

  unsigned getCatchPadExceptionPointerVReg(const Value *CPI,
                                           const TargetRegisterClass *RC);

private:
  void addSEHHandlersForLPads(ArrayRef<const LandingPadInst *> LPads);

  /// LiveOutRegInfo - Information about live out vregs.
  IndexedMap<LiveOutInfo, VirtReg2IndexFunctor> LiveOutRegInfo;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_FUNCTIONLOWERINGINFO_H
