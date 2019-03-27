//==-- AArch64.h - Top-level interface for AArch64  --------------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the LLVM
// AArch64 back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64_H

#include "MCTargetDesc/AArch64MCTargetDesc.h"
#include "Utils/AArch64BaseInfo.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

class AArch64RegisterBankInfo;
class AArch64Subtarget;
class AArch64TargetMachine;
class FunctionPass;
class InstructionSelector;
class MachineFunctionPass;

FunctionPass *createAArch64DeadRegisterDefinitions();
FunctionPass *createAArch64RedundantCopyEliminationPass();
FunctionPass *createAArch64CondBrTuning();
FunctionPass *createAArch64CompressJumpTablesPass();
FunctionPass *createAArch64ConditionalCompares();
FunctionPass *createAArch64AdvSIMDScalar();
FunctionPass *createAArch64ISelDag(AArch64TargetMachine &TM,
                                 CodeGenOpt::Level OptLevel);
FunctionPass *createAArch64StorePairSuppressPass();
FunctionPass *createAArch64ExpandPseudoPass();
FunctionPass *createAArch64SpeculationHardeningPass();
FunctionPass *createAArch64LoadStoreOptimizationPass();
FunctionPass *createAArch64SIMDInstrOptPass();
ModulePass *createAArch64PromoteConstantPass();
FunctionPass *createAArch64ConditionOptimizerPass();
FunctionPass *createAArch64A57FPLoadBalancing();
FunctionPass *createAArch64A53Fix835769();
FunctionPass *createFalkorHWPFFixPass();
FunctionPass *createFalkorMarkStridedAccessesPass();
FunctionPass *createAArch64BranchTargetsPass();

FunctionPass *createAArch64CleanupLocalDynamicTLSPass();

FunctionPass *createAArch64CollectLOHPass();
InstructionSelector *
createAArch64InstructionSelector(const AArch64TargetMachine &,
                                 AArch64Subtarget &, AArch64RegisterBankInfo &);
FunctionPass *createAArch64PreLegalizeCombiner();

void initializeAArch64A53Fix835769Pass(PassRegistry&);
void initializeAArch64A57FPLoadBalancingPass(PassRegistry&);
void initializeAArch64AdvSIMDScalarPass(PassRegistry&);
void initializeAArch64BranchTargetsPass(PassRegistry&);
void initializeAArch64CollectLOHPass(PassRegistry&);
void initializeAArch64CondBrTuningPass(PassRegistry &);
void initializeAArch64CompressJumpTablesPass(PassRegistry&);
void initializeAArch64ConditionalComparesPass(PassRegistry&);
void initializeAArch64ConditionOptimizerPass(PassRegistry&);
void initializeAArch64DeadRegisterDefinitionsPass(PassRegistry&);
void initializeAArch64ExpandPseudoPass(PassRegistry&);
void initializeAArch64SpeculationHardeningPass(PassRegistry&);
void initializeAArch64LoadStoreOptPass(PassRegistry&);
void initializeAArch64SIMDInstrOptPass(PassRegistry&);
void initializeAArch64PreLegalizerCombinerPass(PassRegistry&);
void initializeAArch64PromoteConstantPass(PassRegistry&);
void initializeAArch64RedundantCopyEliminationPass(PassRegistry&);
void initializeAArch64StorePairSuppressPass(PassRegistry&);
void initializeFalkorHWPFFixPass(PassRegistry&);
void initializeFalkorMarkStridedAccessesLegacyPass(PassRegistry&);
void initializeLDTLSCleanupPass(PassRegistry&);
} // end namespace llvm

#endif
