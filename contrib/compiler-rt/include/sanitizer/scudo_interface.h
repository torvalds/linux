//===-- sanitizer/scudo_interface.h -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// Public Scudo interface header.
//
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_SCUDO_INTERFACE_H_
#define SANITIZER_SCUDO_INTERFACE_H_

#include <sanitizer/common_interface_defs.h>

#ifdef __cplusplus
extern "C" {
#endif
  // This function may be optionally provided by a user and should return
  // a string containing Scudo runtime options. See scudo_flags.h for details.
  const char* __scudo_default_options(void);

  // This function allows to set the RSS limit at runtime. This can be either
  // the hard limit (HardLimit=1) or the soft limit (HardLimit=0). The limit
  // can be removed by setting LimitMb to 0. This function's parameters should
  // be fully trusted to avoid security mishaps.
  void __scudo_set_rss_limit(size_t LimitMb, int HardLimit);

  // This function outputs various allocator statistics for both the Primary
  // and Secondary allocators, including memory usage, number of allocations
  // and deallocations.
  void __scudo_print_stats(void);
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // SANITIZER_SCUDO_INTERFACE_H_
