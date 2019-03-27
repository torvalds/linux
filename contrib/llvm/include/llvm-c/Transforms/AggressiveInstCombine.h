/*===-- AggressiveInstCombine.h ---------------------------------*- C++ -*-===*\
|*                                                                            *|
|*                     The LLVM Compiler Infrastructure                       *|
|*                                                                            *|
|* This file is distributed under the University of Illinois Open Source      *|
|* License. See LICENSE.TXT for details.                                      *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header declares the C interface to libLLVMAggressiveInstCombine.a,    *|
|* which combines instructions to form fewer, simple IR instructions.         *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_TRANSFORMS_AGGRESSIVEINSTCOMBINE_H
#define LLVM_C_TRANSFORMS_AGGRESSIVEINSTCOMBINE_H

#include "llvm-c/Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup LLVMCTransformsAggressiveInstCombine Aggressive Instruction Combining transformations
 * @ingroup LLVMCTransforms
 *
 * @{
 */

/** See llvm::createAggressiveInstCombinerPass function. */
void LLVMAddAggressiveInstCombinerPass(LLVMPassManagerRef PM);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif /* defined(__cplusplus) */

#endif

