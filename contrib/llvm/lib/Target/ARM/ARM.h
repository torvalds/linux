//===-- ARM.h - Top-level interface for ARM representation ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the LLVM
// ARM back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARM_H
#define LLVM_LIB_TARGET_ARM_ARM_H

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/CodeGen.h"
#include <functional>
#include <vector>

namespace llvm {

class ARMAsmPrinter;
class ARMBaseTargetMachine;
class ARMRegisterBankInfo;
class ARMSubtarget;
struct BasicBlockInfo;
class Function;
class FunctionPass;
class InstructionSelector;
class MachineBasicBlock;
class MachineFunction;
class MachineInstr;
class MCInst;
class PassRegistry;


Pass *createARMParallelDSPPass();
FunctionPass *createARMISelDag(ARMBaseTargetMachine &TM,
                               CodeGenOpt::Level OptLevel);
FunctionPass *createA15SDOptimizerPass();
FunctionPass *createARMLoadStoreOptimizationPass(bool PreAlloc = false);
FunctionPass *createARMExpandPseudoPass();
FunctionPass *createARMCodeGenPreparePass();
FunctionPass *createARMConstantIslandPass();
FunctionPass *createMLxExpansionPass();
FunctionPass *createThumb2ITBlockPass();
FunctionPass *createARMOptimizeBarriersPass();
FunctionPass *createThumb2SizeReductionPass(
    std::function<bool(const Function &)> Ftor = nullptr);
InstructionSelector *
createARMInstructionSelector(const ARMBaseTargetMachine &TM, const ARMSubtarget &STI,
                             const ARMRegisterBankInfo &RBI);

void LowerARMMachineInstrToMCInst(const MachineInstr *MI, MCInst &OutMI,
                                  ARMAsmPrinter &AP);

void computeBlockSize(MachineFunction *MF, MachineBasicBlock *MBB,
                      BasicBlockInfo &BBI);
std::vector<BasicBlockInfo> computeAllBlockSizes(MachineFunction *MF);


void initializeARMParallelDSPPass(PassRegistry &);
void initializeARMLoadStoreOptPass(PassRegistry &);
void initializeARMPreAllocLoadStoreOptPass(PassRegistry &);
void initializeARMCodeGenPreparePass(PassRegistry &);
void initializeARMConstantIslandsPass(PassRegistry &);
void initializeARMExpandPseudoPass(PassRegistry &);
void initializeThumb2SizeReducePass(PassRegistry &);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARM_ARM_H
