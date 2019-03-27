/*===-- llvm-c/Disassembler.h - Disassembler Public C Interface ---*- C -*-===*\
|*                                                                            *|
|*                     The LLVM Compiler Infrastructure                       *|
|*                                                                            *|
|* This file is distributed under the University of Illinois Open Source      *|
|* License. See LICENSE.TXT for details.                                      *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header provides a public interface to a disassembler library.         *|
|* LLVM provides an implementation of this interface.                         *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_DISASSEMBLER_H
#define LLVM_C_DISASSEMBLER_H

#include "llvm-c/DisassemblerTypes.h"

/**
 * @defgroup LLVMCDisassembler Disassembler
 * @ingroup LLVMC
 *
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif /* !defined(__cplusplus) */

/**
 * Create a disassembler for the TripleName.  Symbolic disassembly is supported
 * by passing a block of information in the DisInfo parameter and specifying the
 * TagType and callback functions as described above.  These can all be passed
 * as NULL.  If successful, this returns a disassembler context.  If not, it
 * returns NULL. This function is equivalent to calling
 * LLVMCreateDisasmCPUFeatures() with an empty CPU name and feature set.
 */
LLVMDisasmContextRef LLVMCreateDisasm(const char *TripleName, void *DisInfo,
                                      int TagType, LLVMOpInfoCallback GetOpInfo,
                                      LLVMSymbolLookupCallback SymbolLookUp);

/**
 * Create a disassembler for the TripleName and a specific CPU.  Symbolic
 * disassembly is supported by passing a block of information in the DisInfo
 * parameter and specifying the TagType and callback functions as described
 * above.  These can all be passed * as NULL.  If successful, this returns a
 * disassembler context.  If not, it returns NULL. This function is equivalent
 * to calling LLVMCreateDisasmCPUFeatures() with an empty feature set.
 */
LLVMDisasmContextRef LLVMCreateDisasmCPU(const char *Triple, const char *CPU,
                                         void *DisInfo, int TagType,
                                         LLVMOpInfoCallback GetOpInfo,
                                         LLVMSymbolLookupCallback SymbolLookUp);

/**
 * Create a disassembler for the TripleName, a specific CPU and specific feature
 * string.  Symbolic disassembly is supported by passing a block of information
 * in the DisInfo parameter and specifying the TagType and callback functions as
 * described above.  These can all be passed * as NULL.  If successful, this
 * returns a disassembler context.  If not, it returns NULL.
 */
LLVMDisasmContextRef
LLVMCreateDisasmCPUFeatures(const char *Triple, const char *CPU,
                            const char *Features, void *DisInfo, int TagType,
                            LLVMOpInfoCallback GetOpInfo,
                            LLVMSymbolLookupCallback SymbolLookUp);

/**
 * Set the disassembler's options.  Returns 1 if it can set the Options and 0
 * otherwise.
 */
int LLVMSetDisasmOptions(LLVMDisasmContextRef DC, uint64_t Options);

/* The option to produce marked up assembly. */
#define LLVMDisassembler_Option_UseMarkup 1
/* The option to print immediates as hex. */
#define LLVMDisassembler_Option_PrintImmHex 2
/* The option use the other assembler printer variant */
#define LLVMDisassembler_Option_AsmPrinterVariant 4
/* The option to set comment on instructions */
#define LLVMDisassembler_Option_SetInstrComments 8
  /* The option to print latency information alongside instructions */
#define LLVMDisassembler_Option_PrintLatency 16

/**
 * Dispose of a disassembler context.
 */
void LLVMDisasmDispose(LLVMDisasmContextRef DC);

/**
 * Disassemble a single instruction using the disassembler context specified in
 * the parameter DC.  The bytes of the instruction are specified in the
 * parameter Bytes, and contains at least BytesSize number of bytes.  The
 * instruction is at the address specified by the PC parameter.  If a valid
 * instruction can be disassembled, its string is returned indirectly in
 * OutString whose size is specified in the parameter OutStringSize.  This
 * function returns the number of bytes in the instruction or zero if there was
 * no valid instruction.
 */
size_t LLVMDisasmInstruction(LLVMDisasmContextRef DC, uint8_t *Bytes,
                             uint64_t BytesSize, uint64_t PC,
                             char *OutString, size_t OutStringSize);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif /* !defined(__cplusplus) */

#endif /* LLVM_C_DISASSEMBLER_H */
