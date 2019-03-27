//===- llvm/CodeGen/GlobalISel/CSEInfo.h ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// Provides analysis for continuously CSEing during GISel passes.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CODEGEN_GLOBALISEL_CSEINFO_H
#define LLVM_CODEGEN_GLOBALISEL_CSEINFO_H

#include "llvm/ADT/FoldingSet.h"
#include "llvm/CodeGen/GlobalISel/GISelChangeObserver.h"
#include "llvm/CodeGen/GlobalISel/GISelWorkList.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Allocator.h"

namespace llvm {

/// A class that wraps MachineInstrs and derives from FoldingSetNode in order to
/// be uniqued in a CSEMap. The tradeoff here is extra memory allocations for
/// UniqueMachineInstr vs making MachineInstr bigger.
class UniqueMachineInstr : public FoldingSetNode {
  friend class GISelCSEInfo;
  const MachineInstr *MI;
  explicit UniqueMachineInstr(const MachineInstr *MI) : MI(MI) {}

public:
  void Profile(FoldingSetNodeID &ID);
};

// Class representing some configuration that can be done during CSE analysis.
// Currently it only supports shouldCSE method that each pass can set.
class CSEConfig {
public:
  virtual ~CSEConfig() = default;
  // Hook for defining which Generic instructions should be CSEd.
  // GISelCSEInfo currently only calls this hook when dealing with generic
  // opcodes.
  virtual bool shouldCSEOpc(unsigned Opc);
};

// TODO: Find a better place for this.
// Commonly used for O0 config.
class CSEConfigConstantOnly : public CSEConfig {
public:
  virtual ~CSEConfigConstantOnly() = default;
  virtual bool shouldCSEOpc(unsigned Opc) override;
};

/// The CSE Analysis object.
/// This installs itself as a delegate to the MachineFunction to track
/// new instructions as well as deletions. It however will not be able to
/// track instruction mutations. In such cases, recordNewInstruction should be
/// called (for eg inside MachineIRBuilder::recordInsertion).
/// Also because of how just the instruction can be inserted without adding any
/// operands to the instruction, instructions are uniqued and inserted lazily.
/// CSEInfo should assert when trying to enter an incomplete instruction into
/// the CSEMap. There is Opcode level granularity on which instructions can be
/// CSE'd and for now, only Generic instructions are CSEable.
class GISelCSEInfo : public GISelChangeObserver {
  // Make it accessible only to CSEMIRBuilder.
  friend class CSEMIRBuilder;

  BumpPtrAllocator UniqueInstrAllocator;
  FoldingSet<UniqueMachineInstr> CSEMap;
  MachineRegisterInfo *MRI = nullptr;
  MachineFunction *MF = nullptr;
  std::unique_ptr<CSEConfig> CSEOpt;
  /// Keep a cache of UniqueInstrs for each MachineInstr. In GISel,
  /// often instructions are mutated (while their ID has completely changed).
  /// Whenever mutation happens, invalidate the UniqueMachineInstr for the
  /// MachineInstr
  DenseMap<const MachineInstr *, UniqueMachineInstr *> InstrMapping;

  /// Store instructions that are not fully formed in TemporaryInsts.
  /// Also because CSE insertion happens lazily, we can remove insts from this
  /// list and avoid inserting and then removing from the CSEMap.
  GISelWorkList<8> TemporaryInsts;

  // Only used in asserts.
  DenseMap<unsigned, unsigned> OpcodeHitTable;

  bool isUniqueMachineInstValid(const UniqueMachineInstr &UMI) const;

  void invalidateUniqueMachineInstr(UniqueMachineInstr *UMI);

  UniqueMachineInstr *getNodeIfExists(FoldingSetNodeID &ID,
                                      MachineBasicBlock *MBB, void *&InsertPos);

  /// Allocate and construct a new UniqueMachineInstr for MI and return.
  UniqueMachineInstr *getUniqueInstrForMI(const MachineInstr *MI);

  void insertNode(UniqueMachineInstr *UMI, void *InsertPos = nullptr);

  /// Get the MachineInstr(Unique) if it exists already in the CSEMap and the
  /// same MachineBasicBlock.
  MachineInstr *getMachineInstrIfExists(FoldingSetNodeID &ID,
                                        MachineBasicBlock *MBB,
                                        void *&InsertPos);

  /// Use this method to allocate a new UniqueMachineInstr for MI and insert it
  /// into the CSEMap. MI should return true for shouldCSE(MI->getOpcode())
  void insertInstr(MachineInstr *MI, void *InsertPos = nullptr);

public:
  GISelCSEInfo() = default;

  virtual ~GISelCSEInfo();

  void setMF(MachineFunction &MF);

  /// Records a newly created inst in a list and lazily insert it to the CSEMap.
  /// Sometimes, this method might be called with a partially constructed
  /// MachineInstr,
  //  (right after BuildMI without adding any operands) - and in such cases,
  //  defer the hashing of the instruction to a later stage.
  void recordNewInstruction(MachineInstr *MI);

  /// Use this callback to inform CSE about a newly fully created instruction.
  void handleRecordedInst(MachineInstr *MI);

  /// Use this callback to insert all the recorded instructions. At this point,
  /// all of these insts need to be fully constructed and should not be missing
  /// any operands.
  void handleRecordedInsts();

  /// Remove this inst from the CSE map. If this inst has not been inserted yet,
  /// it will be removed from the Tempinsts list if it exists.
  void handleRemoveInst(MachineInstr *MI);

  void releaseMemory();

  void setCSEConfig(std::unique_ptr<CSEConfig> Opt) { CSEOpt = std::move(Opt); }

  bool shouldCSE(unsigned Opc) const;

  void analyze(MachineFunction &MF);

  void countOpcodeHit(unsigned Opc);

  void print();

  // Observer API
  void erasingInstr(MachineInstr &MI) override;
  void createdInstr(MachineInstr &MI) override;
  void changingInstr(MachineInstr &MI) override;
  void changedInstr(MachineInstr &MI) override;
};

class TargetRegisterClass;
class RegisterBank;

// Simple builder class to easily profile properties about MIs.
class GISelInstProfileBuilder {
  FoldingSetNodeID &ID;
  const MachineRegisterInfo &MRI;

public:
  GISelInstProfileBuilder(FoldingSetNodeID &ID, const MachineRegisterInfo &MRI)
      : ID(ID), MRI(MRI) {}
  // Profiling methods.
  const GISelInstProfileBuilder &addNodeIDOpcode(unsigned Opc) const;
  const GISelInstProfileBuilder &addNodeIDRegType(const LLT &Ty) const;
  const GISelInstProfileBuilder &addNodeIDRegType(const unsigned) const;

  const GISelInstProfileBuilder &
  addNodeIDRegType(const TargetRegisterClass *RC) const;
  const GISelInstProfileBuilder &addNodeIDRegType(const RegisterBank *RB) const;

  const GISelInstProfileBuilder &addNodeIDRegNum(unsigned Reg) const;

  const GISelInstProfileBuilder &addNodeIDImmediate(int64_t Imm) const;
  const GISelInstProfileBuilder &
  addNodeIDMBB(const MachineBasicBlock *MBB) const;

  const GISelInstProfileBuilder &
  addNodeIDMachineOperand(const MachineOperand &MO) const;

  const GISelInstProfileBuilder &addNodeIDFlag(unsigned Flag) const;
  const GISelInstProfileBuilder &addNodeID(const MachineInstr *MI) const;
};

/// Simple wrapper that does the following.
/// 1) Lazily evaluate the MachineFunction to compute CSEable instructions.
/// 2) Allows configuration of which instructions are CSEd through CSEConfig
/// object. Provides a method called get which takes a CSEConfig object.
class GISelCSEAnalysisWrapper {
  GISelCSEInfo Info;
  MachineFunction *MF = nullptr;
  bool AlreadyComputed = false;

public:
  /// Takes a CSEConfig object that defines what opcodes get CSEd.
  /// If CSEConfig is already set, and the CSE Analysis has been preserved,
  /// it will not use the new CSEOpt(use Recompute to force using the new
  /// CSEOpt).
  GISelCSEInfo &get(std::unique_ptr<CSEConfig> CSEOpt, bool ReCompute = false);
  void setMF(MachineFunction &MFunc) { MF = &MFunc; }
  void setComputed(bool Computed) { AlreadyComputed = Computed; }
  void releaseMemory() { Info.releaseMemory(); }
};

/// The actual analysis pass wrapper.
class GISelCSEAnalysisWrapperPass : public MachineFunctionPass {
  GISelCSEAnalysisWrapper Wrapper;

public:
  static char ID;
  GISelCSEAnalysisWrapperPass() : MachineFunctionPass(ID) {
    initializeGISelCSEAnalysisWrapperPassPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  const GISelCSEAnalysisWrapper &getCSEWrapper() const { return Wrapper; }
  GISelCSEAnalysisWrapper &getCSEWrapper() { return Wrapper; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void releaseMemory() override {
    Wrapper.releaseMemory();
    Wrapper.setComputed(false);
  }
};

} // namespace llvm

#endif
