/*===-- llvm-c/Comdat.h - Module Comdat C Interface -------------*- C++ -*-===*\
|*                                                                            *|
|*                     The LLVM Compiler Infrastructure                       *|
|*                                                                            *|
|* This file is distributed under the University of Illinois Open Source      *|
|* License. See LICENSE.TXT for details.                                      *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This file defines the C interface to COMDAT.                               *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_COMDAT_H
#define LLVM_C_COMDAT_H

#include "llvm-c/Types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  LLVMAnyComdatSelectionKind,        ///< The linker may choose any COMDAT.
  LLVMExactMatchComdatSelectionKind, ///< The data referenced by the COMDAT must
                                     ///< be the same.
  LLVMLargestComdatSelectionKind,    ///< The linker will choose the largest
                                     ///< COMDAT.
  LLVMNoDuplicatesComdatSelectionKind, ///< No other Module may specify this
                                       ///< COMDAT.
  LLVMSameSizeComdatSelectionKind ///< The data referenced by the COMDAT must be
                                  ///< the same size.
} LLVMComdatSelectionKind;

/**
 * Return the Comdat in the module with the specified name. It is created
 * if it didn't already exist.
 *
 * @see llvm::Module::getOrInsertComdat()
 */
LLVMComdatRef LLVMGetOrInsertComdat(LLVMModuleRef M, const char *Name);

/**
 * Get the Comdat assigned to the given global object.
 *
 * @see llvm::GlobalObject::getComdat()
 */
LLVMComdatRef LLVMGetComdat(LLVMValueRef V);

/**
 * Assign the Comdat to the given global object.
 *
 * @see llvm::GlobalObject::setComdat()
 */
void LLVMSetComdat(LLVMValueRef V, LLVMComdatRef C);

/*
 * Get the conflict resolution selection kind for the Comdat.
 *
 * @see llvm::Comdat::getSelectionKind()
 */
LLVMComdatSelectionKind LLVMGetComdatSelectionKind(LLVMComdatRef C);

/*
 * Set the conflict resolution selection kind for the Comdat.
 *
 * @see llvm::Comdat::setSelectionKind()
 */
void LLVMSetComdatSelectionKind(LLVMComdatRef C, LLVMComdatSelectionKind Kind);

#ifdef __cplusplus
}
#endif

#endif
