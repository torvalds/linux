/*===-- llvm-c/Initialization.h - Initialization C Interface ------*- C -*-===*\
|*                                                                            *|
|*                     The LLVM Compiler Infrastructure                       *|
|*                                                                            *|
|* This file is distributed under the University of Illinois Open Source      *|
|* License. See LICENSE.TXT for details.                                      *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header declares the C interface to LLVM initialization routines,      *|
|* which must be called before you can use the functionality provided by      *|
|* the corresponding LLVM library.                                            *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_INITIALIZATION_H
#define LLVM_C_INITIALIZATION_H

#include "llvm-c/Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup LLVMCInitialization Initialization Routines
 * @ingroup LLVMC
 *
 * This module contains routines used to initialize the LLVM system.
 *
 * @{
 */

void LLVMInitializeCore(LLVMPassRegistryRef R);
void LLVMInitializeTransformUtils(LLVMPassRegistryRef R);
void LLVMInitializeScalarOpts(LLVMPassRegistryRef R);
void LLVMInitializeObjCARCOpts(LLVMPassRegistryRef R);
void LLVMInitializeVectorization(LLVMPassRegistryRef R);
void LLVMInitializeInstCombine(LLVMPassRegistryRef R);
void LLVMInitializeAggressiveInstCombiner(LLVMPassRegistryRef R);
void LLVMInitializeIPO(LLVMPassRegistryRef R);
void LLVMInitializeInstrumentation(LLVMPassRegistryRef R);
void LLVMInitializeAnalysis(LLVMPassRegistryRef R);
void LLVMInitializeIPA(LLVMPassRegistryRef R);
void LLVMInitializeCodeGen(LLVMPassRegistryRef R);
void LLVMInitializeTarget(LLVMPassRegistryRef R);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif
