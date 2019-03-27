/*===----------- llvm-c/OrcBindings.h - Orc Lib C Iface ---------*- C++ -*-===*\
|*                                                                            *|
|*                     The LLVM Compiler Infrastructure                       *|
|*                                                                            *|
|* This file is distributed under the University of Illinois Open Source      *|
|* License. See LICENSE.TXT for details.                                      *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header declares the C interface to libLLVMOrcJIT.a, which implements  *|
|* JIT compilation of LLVM IR.                                                *|
|*                                                                            *|
|* Many exotic languages can interoperate with C code but have a harder time  *|
|* with C++ due to name mangling. So in addition to C, this interface enables *|
|* tools written in such languages.                                           *|
|*                                                                            *|
|* Note: This interface is experimental. It is *NOT* stable, and may be       *|
|*       changed without warning.                                             *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_ORCBINDINGS_H
#define LLVM_C_ORCBINDINGS_H

#include "llvm-c/Error.h"
#include "llvm-c/Object.h"
#include "llvm-c/TargetMachine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LLVMOrcOpaqueJITStack *LLVMOrcJITStackRef;
typedef uint64_t LLVMOrcModuleHandle;
typedef uint64_t LLVMOrcTargetAddress;
typedef uint64_t (*LLVMOrcSymbolResolverFn)(const char *Name, void *LookupCtx);
typedef uint64_t (*LLVMOrcLazyCompileCallbackFn)(LLVMOrcJITStackRef JITStack,
                                                 void *CallbackCtx);

/**
 * Create an ORC JIT stack.
 *
 * The client owns the resulting stack, and must call OrcDisposeInstance(...)
 * to destroy it and free its memory. The JIT stack will take ownership of the
 * TargetMachine, which will be destroyed when the stack is destroyed. The
 * client should not attempt to dispose of the Target Machine, or it will result
 * in a double-free.
 */
LLVMOrcJITStackRef LLVMOrcCreateInstance(LLVMTargetMachineRef TM);

/**
 * Get the error message for the most recent error (if any).
 *
 * This message is owned by the ORC JIT Stack and will be freed when the stack
 * is disposed of by LLVMOrcDisposeInstance.
 */
const char *LLVMOrcGetErrorMsg(LLVMOrcJITStackRef JITStack);

/**
 * Mangle the given symbol.
 * Memory will be allocated for MangledSymbol to hold the result. The client
 */
void LLVMOrcGetMangledSymbol(LLVMOrcJITStackRef JITStack, char **MangledSymbol,
                             const char *Symbol);

/**
 * Dispose of a mangled symbol.
 */
void LLVMOrcDisposeMangledSymbol(char *MangledSymbol);

/**
 * Create a lazy compile callback.
 */
LLVMErrorRef LLVMOrcCreateLazyCompileCallback(
    LLVMOrcJITStackRef JITStack, LLVMOrcTargetAddress *RetAddr,
    LLVMOrcLazyCompileCallbackFn Callback, void *CallbackCtx);

/**
 * Create a named indirect call stub.
 */
LLVMErrorRef LLVMOrcCreateIndirectStub(LLVMOrcJITStackRef JITStack,
                                       const char *StubName,
                                       LLVMOrcTargetAddress InitAddr);

/**
 * Set the pointer for the given indirect stub.
 */
LLVMErrorRef LLVMOrcSetIndirectStubPointer(LLVMOrcJITStackRef JITStack,
                                           const char *StubName,
                                           LLVMOrcTargetAddress NewAddr);

/**
 * Add module to be eagerly compiled.
 */
LLVMErrorRef LLVMOrcAddEagerlyCompiledIR(LLVMOrcJITStackRef JITStack,
                                         LLVMOrcModuleHandle *RetHandle,
                                         LLVMModuleRef Mod,
                                         LLVMOrcSymbolResolverFn SymbolResolver,
                                         void *SymbolResolverCtx);

/**
 * Add module to be lazily compiled one function at a time.
 */
LLVMErrorRef LLVMOrcAddLazilyCompiledIR(LLVMOrcJITStackRef JITStack,
                                        LLVMOrcModuleHandle *RetHandle,
                                        LLVMModuleRef Mod,
                                        LLVMOrcSymbolResolverFn SymbolResolver,
                                        void *SymbolResolverCtx);

/**
 * Add an object file.
 *
 * This method takes ownership of the given memory buffer and attempts to add
 * it to the JIT as an object file.
 * Clients should *not* dispose of the 'Obj' argument: the JIT will manage it
 * from this call onwards.
 */
LLVMErrorRef LLVMOrcAddObjectFile(LLVMOrcJITStackRef JITStack,
                                  LLVMOrcModuleHandle *RetHandle,
                                  LLVMMemoryBufferRef Obj,
                                  LLVMOrcSymbolResolverFn SymbolResolver,
                                  void *SymbolResolverCtx);

/**
 * Remove a module set from the JIT.
 *
 * This works for all modules that can be added via OrcAdd*, including object
 * files.
 */
LLVMErrorRef LLVMOrcRemoveModule(LLVMOrcJITStackRef JITStack,
                                 LLVMOrcModuleHandle H);

/**
 * Get symbol address from JIT instance.
 */
LLVMErrorRef LLVMOrcGetSymbolAddress(LLVMOrcJITStackRef JITStack,
                                     LLVMOrcTargetAddress *RetAddr,
                                     const char *SymbolName);

/**
 * Get symbol address from JIT instance, searching only the specified
 * handle.
 */
LLVMErrorRef LLVMOrcGetSymbolAddressIn(LLVMOrcJITStackRef JITStack,
                                       LLVMOrcTargetAddress *RetAddr,
                                       LLVMOrcModuleHandle H,
                                       const char *SymbolName);

/**
 * Dispose of an ORC JIT stack.
 */
LLVMErrorRef LLVMOrcDisposeInstance(LLVMOrcJITStackRef JITStack);

/**
 * Register a JIT Event Listener.
 *
 * A NULL listener is ignored.
 */
void LLVMOrcRegisterJITEventListener(LLVMOrcJITStackRef JITStack, LLVMJITEventListenerRef L);

/**
 * Unegister a JIT Event Listener.
 *
 * A NULL listener is ignored.
 */
void LLVMOrcUnregisterJITEventListener(LLVMOrcJITStackRef JITStack, LLVMJITEventListenerRef L);

#ifdef __cplusplus
}
#endif /* extern "C" */

#endif /* LLVM_C_ORCBINDINGS_H */
