//===-- scudo_interface_internal.h ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// Private Scudo interface header.
///
//===----------------------------------------------------------------------===//

#ifndef SCUDO_INTERFACE_INTERNAL_H_
#define SCUDO_INTERFACE_INTERNAL_H_

#include "sanitizer_common/sanitizer_internal_defs.h"

using __sanitizer::uptr;
using __sanitizer::s32;

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE
const char* __scudo_default_options();

SANITIZER_INTERFACE_ATTRIBUTE
void __scudo_set_rss_limit(uptr LimitMb, s32 HardLimit);

SANITIZER_INTERFACE_ATTRIBUTE
void __scudo_print_stats();
}  // extern "C"

#endif  // SCUDO_INTERFACE_INTERNAL_H_
