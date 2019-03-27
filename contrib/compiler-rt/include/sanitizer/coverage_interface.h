//===-- sanitizer/coverage_interface.h --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Public interface for sanitizer coverage.
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_COVERAG_INTERFACE_H
#define SANITIZER_COVERAG_INTERFACE_H

#include <sanitizer/common_interface_defs.h>

#ifdef __cplusplus
extern "C" {
#endif

  // Record and dump coverage info.
  void __sanitizer_cov_dump(void);

  // Clear collected coverage info.
  void __sanitizer_cov_reset(void);

  // Dump collected coverage info. Sorts pcs by module into individual .sancov
  // files.
  void __sanitizer_dump_coverage(const uintptr_t *pcs, uintptr_t len);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // SANITIZER_COVERAG_INTERFACE_H
