//===-- llvm/LinkTimeOptimizer.h - LTO Public C Interface -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header provides a C API to use the LLVM link time optimization
// library. This is intended to be used by linkers which are C-only in
// their implementation for performing LTO.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_C_LINKTIMEOPTIMIZER_H
#define LLVM_C_LINKTIMEOPTIMIZER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup LLVMCLinkTimeOptimizer Link Time Optimization
 * @ingroup LLVMC
 *
 * @{
 */

  /// This provides a dummy type for pointers to the LTO object.
  typedef void* llvm_lto_t;

  /// This provides a C-visible enumerator to manage status codes.
  /// This should map exactly onto the C++ enumerator LTOStatus.
  typedef enum llvm_lto_status {
    LLVM_LTO_UNKNOWN,
    LLVM_LTO_OPT_SUCCESS,
    LLVM_LTO_READ_SUCCESS,
    LLVM_LTO_READ_FAILURE,
    LLVM_LTO_WRITE_FAILURE,
    LLVM_LTO_NO_TARGET,
    LLVM_LTO_NO_WORK,
    LLVM_LTO_MODULE_MERGE_FAILURE,
    LLVM_LTO_ASM_FAILURE,

    //  Added C-specific error codes
    LLVM_LTO_NULL_OBJECT
  } llvm_lto_status_t;

  /// This provides C interface to initialize link time optimizer. This allows
  /// linker to use dlopen() interface to dynamically load LinkTimeOptimizer.
  /// extern "C" helps, because dlopen() interface uses name to find the symbol.
  extern llvm_lto_t llvm_create_optimizer(void);
  extern void llvm_destroy_optimizer(llvm_lto_t lto);

  extern llvm_lto_status_t llvm_read_object_file
    (llvm_lto_t lto, const char* input_filename);
  extern llvm_lto_status_t llvm_optimize_modules
    (llvm_lto_t lto, const char* output_filename);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif
