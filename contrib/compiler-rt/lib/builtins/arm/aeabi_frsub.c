//===-- lib/arm/aeabi_frsub.c - Single-precision subtraction --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define SINGLE_PRECISION
#include "../fp_lib.h"

AEABI_RTABI fp_t
__aeabi_fsub(fp_t, fp_t);

AEABI_RTABI fp_t
__aeabi_frsub(fp_t a, fp_t b) {
    return __aeabi_fsub(b, a);
}
