//===- StackProtector.h - Stack Protector Insertion -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass inserts stack protectors into functions which need them. A variable
// with a random value in it is stored onto the stack before the local variables
// are allocated. Upon exiting the block, the stored value is checked. If it's
// changed, then there was some sort of violation and the program aborts.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_STACKPROTECTOR_H
#define LLVM_CODEGEN_STACKPROTECTOR_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Triple.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Pass.h"

namespace llvm {

class BasicBlock;
class DominatorTree;
class Function;
class Instruction;
class Module;
class TargetLoweringBase;
class TargetMachine;
class Type;

class StackProtector : public FunctionPass {
private:
  /// A mapping of AllocaInsts to their required SSP layout.
  using SSPLayoutMap = DenseMap<const AllocaInst *,
                                MachineFrameInfo::SSPLayoutKind>;

  const TargetMachine *TM = nullptr;

  /// TLI - Keep a pointer of a TargetLowering to consult for determining
  /// target type sizes.
  const TargetLoweringBase *TLI = nullptr;
  Triple Trip;

  Function *F;
  Module *M;

  DominatorTree *DT;

  /// Layout - Mapping of allocations to the required SSPLayoutKind.
  /// StackProtector analysis will update this map when determining if an
  /// AllocaInst triggers a stack protector.
  SSPLayoutMap Layout;

  /// The minimum size of buffers that will receive stack smashing
  /// protection when -fstack-protection is used.
  unsigned SSPBufferSize = 0;

  /// VisitedPHIs - The set of PHI nodes visited when determining
  /// if a variable's reference has been taken.  This set
  /// is maintained to ensure we don't visit the same PHI node multiple
  /// times.
  SmallPtrSet<const PHINode *, 16> VisitedPHIs;

  // A prologue is generated.
  bool HasPrologue = false;

  // IR checking code is generated.
  bool HasIRCheck = false;

  /// InsertStackProtectors - Insert code into the prologue and epilogue of
  /// the function.
  ///
  ///  - The prologue code loads and stores the stack guard onto the stack.
  ///  - The epilogue checks the value stored in the prologue against the
  ///    original value. It calls __stack_chk_fail if they differ.
  bool InsertStackProtectors();

  /// CreateFailBB - Create a basic block to jump to when the stack protector
  /// check fails.
  BasicBlock *CreateFailBB();

  /// ContainsProtectableArray - Check whether the type either is an array or
  /// contains an array of sufficient size so that we need stack protectors
  /// for it.
  /// \param [out] IsLarge is set to true if a protectable array is found and
  /// it is "large" ( >= ssp-buffer-size).  In the case of a structure with
  /// multiple arrays, this gets set if any of them is large.
  bool ContainsProtectableArray(Type *Ty, bool &IsLarge, bool Strong = false,
                                bool InStruct = false) const;

  /// Check whether a stack allocation has its address taken.
  bool HasAddressTaken(const Instruction *AI);

  /// RequiresStackProtector - Check whether or not this function needs a
  /// stack protector based upon the stack protector level.
  bool RequiresStackProtector();

public:
  static char ID; // Pass identification, replacement for typeid.

  StackProtector() : FunctionPass(ID), SSPBufferSize(8) {
    initializeStackProtectorPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  // Return true if StackProtector is supposed to be handled by SelectionDAG.
  bool shouldEmitSDCheck(const BasicBlock &BB) const;

  bool runOnFunction(Function &Fn) override;

  void copyToMachineFrameInfo(MachineFrameInfo &MFI) const;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_STACKPROTECTOR_H
