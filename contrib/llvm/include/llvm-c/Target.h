/*===-- llvm-c/Target.h - Target Lib C Iface --------------------*- C++ -*-===*/
/*                                                                            */
/*                     The LLVM Compiler Infrastructure                       */
/*                                                                            */
/* This file is distributed under the University of Illinois Open Source      */
/* License. See LICENSE.TXT for details.                                      */
/*                                                                            */
/*===----------------------------------------------------------------------===*/
/*                                                                            */
/* This header declares the C interface to libLLVMTarget.a, which             */
/* implements target information.                                             */
/*                                                                            */
/* Many exotic languages can interoperate with C code but have a harder time  */
/* with C++ due to name mangling. So in addition to C, this interface enables */
/* tools written in such languages.                                           */
/*                                                                            */
/*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_TARGET_H
#define LLVM_C_TARGET_H

#include "llvm-c/Types.h"
#include "llvm/Config/llvm-config.h"

#if defined(_MSC_VER) && !defined(inline)
#define inline __inline
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup LLVMCTarget Target information
 * @ingroup LLVMC
 *
 * @{
 */

enum LLVMByteOrdering { LLVMBigEndian, LLVMLittleEndian };

typedef struct LLVMOpaqueTargetData *LLVMTargetDataRef;
typedef struct LLVMOpaqueTargetLibraryInfotData *LLVMTargetLibraryInfoRef;

/* Declare all of the target-initialization functions that are available. */
#define LLVM_TARGET(TargetName) \
  void LLVMInitialize##TargetName##TargetInfo(void);
#include "llvm/Config/Targets.def"
#undef LLVM_TARGET  /* Explicit undef to make SWIG happier */

#define LLVM_TARGET(TargetName) void LLVMInitialize##TargetName##Target(void);
#include "llvm/Config/Targets.def"
#undef LLVM_TARGET  /* Explicit undef to make SWIG happier */

#define LLVM_TARGET(TargetName) \
  void LLVMInitialize##TargetName##TargetMC(void);
#include "llvm/Config/Targets.def"
#undef LLVM_TARGET  /* Explicit undef to make SWIG happier */

/* Declare all of the available assembly printer initialization functions. */
#define LLVM_ASM_PRINTER(TargetName) \
  void LLVMInitialize##TargetName##AsmPrinter(void);
#include "llvm/Config/AsmPrinters.def"
#undef LLVM_ASM_PRINTER  /* Explicit undef to make SWIG happier */

/* Declare all of the available assembly parser initialization functions. */
#define LLVM_ASM_PARSER(TargetName) \
  void LLVMInitialize##TargetName##AsmParser(void);
#include "llvm/Config/AsmParsers.def"
#undef LLVM_ASM_PARSER  /* Explicit undef to make SWIG happier */

/* Declare all of the available disassembler initialization functions. */
#define LLVM_DISASSEMBLER(TargetName) \
  void LLVMInitialize##TargetName##Disassembler(void);
#include "llvm/Config/Disassemblers.def"
#undef LLVM_DISASSEMBLER  /* Explicit undef to make SWIG happier */

/** LLVMInitializeAllTargetInfos - The main program should call this function if
    it wants access to all available targets that LLVM is configured to
    support. */
static inline void LLVMInitializeAllTargetInfos(void) {
#define LLVM_TARGET(TargetName) LLVMInitialize##TargetName##TargetInfo();
#include "llvm/Config/Targets.def"
#undef LLVM_TARGET  /* Explicit undef to make SWIG happier */
}

/** LLVMInitializeAllTargets - The main program should call this function if it
    wants to link in all available targets that LLVM is configured to
    support. */
static inline void LLVMInitializeAllTargets(void) {
#define LLVM_TARGET(TargetName) LLVMInitialize##TargetName##Target();
#include "llvm/Config/Targets.def"
#undef LLVM_TARGET  /* Explicit undef to make SWIG happier */
}

/** LLVMInitializeAllTargetMCs - The main program should call this function if
    it wants access to all available target MC that LLVM is configured to
    support. */
static inline void LLVMInitializeAllTargetMCs(void) {
#define LLVM_TARGET(TargetName) LLVMInitialize##TargetName##TargetMC();
#include "llvm/Config/Targets.def"
#undef LLVM_TARGET  /* Explicit undef to make SWIG happier */
}

/** LLVMInitializeAllAsmPrinters - The main program should call this function if
    it wants all asm printers that LLVM is configured to support, to make them
    available via the TargetRegistry. */
static inline void LLVMInitializeAllAsmPrinters(void) {
#define LLVM_ASM_PRINTER(TargetName) LLVMInitialize##TargetName##AsmPrinter();
#include "llvm/Config/AsmPrinters.def"
#undef LLVM_ASM_PRINTER  /* Explicit undef to make SWIG happier */
}

/** LLVMInitializeAllAsmParsers - The main program should call this function if
    it wants all asm parsers that LLVM is configured to support, to make them
    available via the TargetRegistry. */
static inline void LLVMInitializeAllAsmParsers(void) {
#define LLVM_ASM_PARSER(TargetName) LLVMInitialize##TargetName##AsmParser();
#include "llvm/Config/AsmParsers.def"
#undef LLVM_ASM_PARSER  /* Explicit undef to make SWIG happier */
}

/** LLVMInitializeAllDisassemblers - The main program should call this function
    if it wants all disassemblers that LLVM is configured to support, to make
    them available via the TargetRegistry. */
static inline void LLVMInitializeAllDisassemblers(void) {
#define LLVM_DISASSEMBLER(TargetName) \
  LLVMInitialize##TargetName##Disassembler();
#include "llvm/Config/Disassemblers.def"
#undef LLVM_DISASSEMBLER  /* Explicit undef to make SWIG happier */
}

/** LLVMInitializeNativeTarget - The main program should call this function to
    initialize the native target corresponding to the host.  This is useful
    for JIT applications to ensure that the target gets linked in correctly. */
static inline LLVMBool LLVMInitializeNativeTarget(void) {
  /* If we have a native target, initialize it to ensure it is linked in. */
#ifdef LLVM_NATIVE_TARGET
  LLVM_NATIVE_TARGETINFO();
  LLVM_NATIVE_TARGET();
  LLVM_NATIVE_TARGETMC();
  return 0;
#else
  return 1;
#endif
}

/** LLVMInitializeNativeTargetAsmParser - The main program should call this
    function to initialize the parser for the native target corresponding to the
    host. */
static inline LLVMBool LLVMInitializeNativeAsmParser(void) {
#ifdef LLVM_NATIVE_ASMPARSER
  LLVM_NATIVE_ASMPARSER();
  return 0;
#else
  return 1;
#endif
}

/** LLVMInitializeNativeTargetAsmPrinter - The main program should call this
    function to initialize the printer for the native target corresponding to
    the host. */
static inline LLVMBool LLVMInitializeNativeAsmPrinter(void) {
#ifdef LLVM_NATIVE_ASMPRINTER
  LLVM_NATIVE_ASMPRINTER();
  return 0;
#else
  return 1;
#endif
}

/** LLVMInitializeNativeTargetDisassembler - The main program should call this
    function to initialize the disassembler for the native target corresponding
    to the host. */
static inline LLVMBool LLVMInitializeNativeDisassembler(void) {
#ifdef LLVM_NATIVE_DISASSEMBLER
  LLVM_NATIVE_DISASSEMBLER();
  return 0;
#else
  return 1;
#endif
}

/*===-- Target Data -------------------------------------------------------===*/

/**
 * Obtain the data layout for a module.
 *
 * @see Module::getDataLayout()
 */
LLVMTargetDataRef LLVMGetModuleDataLayout(LLVMModuleRef M);

/**
 * Set the data layout for a module.
 *
 * @see Module::setDataLayout()
 */
void LLVMSetModuleDataLayout(LLVMModuleRef M, LLVMTargetDataRef DL);

/** Creates target data from a target layout string.
    See the constructor llvm::DataLayout::DataLayout. */
LLVMTargetDataRef LLVMCreateTargetData(const char *StringRep);

/** Deallocates a TargetData.
    See the destructor llvm::DataLayout::~DataLayout. */
void LLVMDisposeTargetData(LLVMTargetDataRef TD);

/** Adds target library information to a pass manager. This does not take
    ownership of the target library info.
    See the method llvm::PassManagerBase::add. */
void LLVMAddTargetLibraryInfo(LLVMTargetLibraryInfoRef TLI,
                              LLVMPassManagerRef PM);

/** Converts target data to a target layout string. The string must be disposed
    with LLVMDisposeMessage.
    See the constructor llvm::DataLayout::DataLayout. */
char *LLVMCopyStringRepOfTargetData(LLVMTargetDataRef TD);

/** Returns the byte order of a target, either LLVMBigEndian or
    LLVMLittleEndian.
    See the method llvm::DataLayout::isLittleEndian. */
enum LLVMByteOrdering LLVMByteOrder(LLVMTargetDataRef TD);

/** Returns the pointer size in bytes for a target.
    See the method llvm::DataLayout::getPointerSize. */
unsigned LLVMPointerSize(LLVMTargetDataRef TD);

/** Returns the pointer size in bytes for a target for a specified
    address space.
    See the method llvm::DataLayout::getPointerSize. */
unsigned LLVMPointerSizeForAS(LLVMTargetDataRef TD, unsigned AS);

/** Returns the integer type that is the same size as a pointer on a target.
    See the method llvm::DataLayout::getIntPtrType. */
LLVMTypeRef LLVMIntPtrType(LLVMTargetDataRef TD);

/** Returns the integer type that is the same size as a pointer on a target.
    This version allows the address space to be specified.
    See the method llvm::DataLayout::getIntPtrType. */
LLVMTypeRef LLVMIntPtrTypeForAS(LLVMTargetDataRef TD, unsigned AS);

/** Returns the integer type that is the same size as a pointer on a target.
    See the method llvm::DataLayout::getIntPtrType. */
LLVMTypeRef LLVMIntPtrTypeInContext(LLVMContextRef C, LLVMTargetDataRef TD);

/** Returns the integer type that is the same size as a pointer on a target.
    This version allows the address space to be specified.
    See the method llvm::DataLayout::getIntPtrType. */
LLVMTypeRef LLVMIntPtrTypeForASInContext(LLVMContextRef C, LLVMTargetDataRef TD,
                                         unsigned AS);

/** Computes the size of a type in bytes for a target.
    See the method llvm::DataLayout::getTypeSizeInBits. */
unsigned long long LLVMSizeOfTypeInBits(LLVMTargetDataRef TD, LLVMTypeRef Ty);

/** Computes the storage size of a type in bytes for a target.
    See the method llvm::DataLayout::getTypeStoreSize. */
unsigned long long LLVMStoreSizeOfType(LLVMTargetDataRef TD, LLVMTypeRef Ty);

/** Computes the ABI size of a type in bytes for a target.
    See the method llvm::DataLayout::getTypeAllocSize. */
unsigned long long LLVMABISizeOfType(LLVMTargetDataRef TD, LLVMTypeRef Ty);

/** Computes the ABI alignment of a type in bytes for a target.
    See the method llvm::DataLayout::getTypeABISize. */
unsigned LLVMABIAlignmentOfType(LLVMTargetDataRef TD, LLVMTypeRef Ty);

/** Computes the call frame alignment of a type in bytes for a target.
    See the method llvm::DataLayout::getTypeABISize. */
unsigned LLVMCallFrameAlignmentOfType(LLVMTargetDataRef TD, LLVMTypeRef Ty);

/** Computes the preferred alignment of a type in bytes for a target.
    See the method llvm::DataLayout::getTypeABISize. */
unsigned LLVMPreferredAlignmentOfType(LLVMTargetDataRef TD, LLVMTypeRef Ty);

/** Computes the preferred alignment of a global variable in bytes for a target.
    See the method llvm::DataLayout::getPreferredAlignment. */
unsigned LLVMPreferredAlignmentOfGlobal(LLVMTargetDataRef TD,
                                        LLVMValueRef GlobalVar);

/** Computes the structure element that contains the byte offset for a target.
    See the method llvm::StructLayout::getElementContainingOffset. */
unsigned LLVMElementAtOffset(LLVMTargetDataRef TD, LLVMTypeRef StructTy,
                             unsigned long long Offset);

/** Computes the byte offset of the indexed struct element for a target.
    See the method llvm::StructLayout::getElementContainingOffset. */
unsigned long long LLVMOffsetOfElement(LLVMTargetDataRef TD,
                                       LLVMTypeRef StructTy, unsigned Element);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif /* defined(__cplusplus) */

#endif
