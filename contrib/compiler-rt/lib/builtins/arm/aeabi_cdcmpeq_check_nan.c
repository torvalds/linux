//===-- lib/arm/aeabi_cdcmpeq_helper.c - Helper for cdcmpeq ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <stdint.h>
#include "../int_lib.h"

AEABI_RTABI __attribute__((visibility("hidden")))
int __aeabi_cdcmpeq_check_nan(double a, double b) {
    return __builtin_isnan(a) || __builtin_isnan(b);
}
