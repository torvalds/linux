//===-- sanitizer/esan_interface.h ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of EfficiencySanitizer, a family of performance tuners.
//
// Public interface header.
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_ESAN_INTERFACE_H
#define SANITIZER_ESAN_INTERFACE_H

#include <sanitizer/common_interface_defs.h>

// We declare our interface routines as weak to allow the user to avoid
// ifdefs and instead use this pattern to allow building the same sources
// with and without our runtime library:
//     if (__esan_report)
//       __esan_report();
#ifdef _MSC_VER
/* selectany is as close to weak as we'll get. */
#define COMPILER_RT_WEAK __declspec(selectany)
#elif __GNUC__
#define COMPILER_RT_WEAK __attribute__((weak))
#else
#define COMPILER_RT_WEAK
#endif

#ifdef __cplusplus
extern "C" {
#endif

// This function can be called mid-run (or at the end of a run for
// a server process that doesn't shut down normally) to request that
// data for that point in the run be reported from the tool.
void COMPILER_RT_WEAK __esan_report(void);

// This function returns the number of samples that the esan tool has collected
// to this point.  This is useful for testing.
unsigned int COMPILER_RT_WEAK __esan_get_sample_count(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SANITIZER_ESAN_INTERFACE_H
