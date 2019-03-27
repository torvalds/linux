/*===-- Scalar.h - Scalar Transformation Library C Interface ----*- C++ -*-===*\
|*                                                                            *|
|*                     The LLVM Compiler Infrastructure                       *|
|*                                                                            *|
|* This file is distributed under the University of Illinois Open Source      *|
|* License. See LICENSE.TXT for details.                                      *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header declares the C interface to libLLVMScalarOpts.a, which         *|
|* implements various scalar transformations of the LLVM IR.                  *|
|*                                                                            *|
|* Many exotic languages can interoperate with C code but have a harder time  *|
|* with C++ due to name mangling. So in addition to C, this interface enables *|
|* tools written in such languages.                                           *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_TRANSFORMS_SCALAR_H
#define LLVM_C_TRANSFORMS_SCALAR_H

#include "llvm-c/Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup LLVMCTransformsScalar Scalar transformations
 * @ingroup LLVMCTransforms
 *
 * @{
 */

/** See llvm::createAggressiveDCEPass function. */
void LLVMAddAggressiveDCEPass(LLVMPassManagerRef PM);

/** See llvm::createBitTrackingDCEPass function. */
void LLVMAddBitTrackingDCEPass(LLVMPassManagerRef PM);

/** See llvm::createAlignmentFromAssumptionsPass function. */
void LLVMAddAlignmentFromAssumptionsPass(LLVMPassManagerRef PM);

/** See llvm::createCFGSimplificationPass function. */
void LLVMAddCFGSimplificationPass(LLVMPassManagerRef PM);

/** See llvm::createDeadStoreEliminationPass function. */
void LLVMAddDeadStoreEliminationPass(LLVMPassManagerRef PM);

/** See llvm::createScalarizerPass function. */
void LLVMAddScalarizerPass(LLVMPassManagerRef PM);

/** See llvm::createMergedLoadStoreMotionPass function. */
void LLVMAddMergedLoadStoreMotionPass(LLVMPassManagerRef PM);

/** See llvm::createGVNPass function. */
void LLVMAddGVNPass(LLVMPassManagerRef PM);

/** See llvm::createGVNPass function. */
void LLVMAddNewGVNPass(LLVMPassManagerRef PM);

/** See llvm::createIndVarSimplifyPass function. */
void LLVMAddIndVarSimplifyPass(LLVMPassManagerRef PM);

/** See llvm::createInstructionCombiningPass function. */
void LLVMAddInstructionCombiningPass(LLVMPassManagerRef PM);

/** See llvm::createJumpThreadingPass function. */
void LLVMAddJumpThreadingPass(LLVMPassManagerRef PM);

/** See llvm::createLICMPass function. */
void LLVMAddLICMPass(LLVMPassManagerRef PM);

/** See llvm::createLoopDeletionPass function. */
void LLVMAddLoopDeletionPass(LLVMPassManagerRef PM);

/** See llvm::createLoopIdiomPass function */
void LLVMAddLoopIdiomPass(LLVMPassManagerRef PM);

/** See llvm::createLoopRotatePass function. */
void LLVMAddLoopRotatePass(LLVMPassManagerRef PM);

/** See llvm::createLoopRerollPass function. */
void LLVMAddLoopRerollPass(LLVMPassManagerRef PM);

/** See llvm::createLoopUnrollPass function. */
void LLVMAddLoopUnrollPass(LLVMPassManagerRef PM);

/** See llvm::createLoopUnrollAndJamPass function. */
void LLVMAddLoopUnrollAndJamPass(LLVMPassManagerRef PM);

/** See llvm::createLoopUnswitchPass function. */
void LLVMAddLoopUnswitchPass(LLVMPassManagerRef PM);

/** See llvm::createLowerAtomicPass function. */
void LLVMAddLowerAtomicPass(LLVMPassManagerRef PM);

/** See llvm::createMemCpyOptPass function. */
void LLVMAddMemCpyOptPass(LLVMPassManagerRef PM);

/** See llvm::createPartiallyInlineLibCallsPass function. */
void LLVMAddPartiallyInlineLibCallsPass(LLVMPassManagerRef PM);

/** See llvm::createReassociatePass function. */
void LLVMAddReassociatePass(LLVMPassManagerRef PM);

/** See llvm::createSCCPPass function. */
void LLVMAddSCCPPass(LLVMPassManagerRef PM);

/** See llvm::createSROAPass function. */
void LLVMAddScalarReplAggregatesPass(LLVMPassManagerRef PM);

/** See llvm::createSROAPass function. */
void LLVMAddScalarReplAggregatesPassSSA(LLVMPassManagerRef PM);

/** See llvm::createSROAPass function. */
void LLVMAddScalarReplAggregatesPassWithThreshold(LLVMPassManagerRef PM,
                                                  int Threshold);

/** See llvm::createSimplifyLibCallsPass function. */
void LLVMAddSimplifyLibCallsPass(LLVMPassManagerRef PM);

/** See llvm::createTailCallEliminationPass function. */
void LLVMAddTailCallEliminationPass(LLVMPassManagerRef PM);

/** See llvm::createConstantPropagationPass function. */
void LLVMAddConstantPropagationPass(LLVMPassManagerRef PM);

/** See llvm::demotePromoteMemoryToRegisterPass function. */
void LLVMAddDemoteMemoryToRegisterPass(LLVMPassManagerRef PM);

/** See llvm::createVerifierPass function. */
void LLVMAddVerifierPass(LLVMPassManagerRef PM);

/** See llvm::createCorrelatedValuePropagationPass function */
void LLVMAddCorrelatedValuePropagationPass(LLVMPassManagerRef PM);

/** See llvm::createEarlyCSEPass function */
void LLVMAddEarlyCSEPass(LLVMPassManagerRef PM);

/** See llvm::createEarlyCSEPass function */
void LLVMAddEarlyCSEMemSSAPass(LLVMPassManagerRef PM);

/** See llvm::createLowerExpectIntrinsicPass function */
void LLVMAddLowerExpectIntrinsicPass(LLVMPassManagerRef PM);

/** See llvm::createTypeBasedAliasAnalysisPass function */
void LLVMAddTypeBasedAliasAnalysisPass(LLVMPassManagerRef PM);

/** See llvm::createScopedNoAliasAAPass function */
void LLVMAddScopedNoAliasAAPass(LLVMPassManagerRef PM);

/** See llvm::createBasicAliasAnalysisPass function */
void LLVMAddBasicAliasAnalysisPass(LLVMPassManagerRef PM);

/** See llvm::createUnifyFunctionExitNodesPass function */
void LLVMAddUnifyFunctionExitNodesPass(LLVMPassManagerRef PM);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif /* defined(__cplusplus) */

#endif
