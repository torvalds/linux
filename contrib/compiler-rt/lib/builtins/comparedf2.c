//===-- lib/comparedf2.c - Double-precision comparisons -----------*- C -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// // This file implements the following soft-float comparison routines:
//
//   __eqdf2   __gedf2   __unorddf2
//   __ledf2   __gtdf2
//   __ltdf2
//   __nedf2
//
// The semantics of the routines grouped in each column are identical, so there
// is a single implementation for each, and wrappers to provide the other names.
//
// The main routines behave as follows:
//
//   __ledf2(a,b) returns -1 if a < b
//                         0 if a == b
//                         1 if a > b
//                         1 if either a or b is NaN
//
//   __gedf2(a,b) returns -1 if a < b
//                         0 if a == b
//                         1 if a > b
//                        -1 if either a or b is NaN
//
//   __unorddf2(a,b) returns 0 if both a and b are numbers
//                           1 if either a or b is NaN
//
// Note that __ledf2( ) and __gedf2( ) are identical except in their handling of
// NaN values.
//
//===----------------------------------------------------------------------===//

#define DOUBLE_PRECISION
#include "fp_lib.h"

enum LE_RESULT {
    LE_LESS      = -1,
    LE_EQUAL     =  0,
    LE_GREATER   =  1,
    LE_UNORDERED =  1
};

COMPILER_RT_ABI enum LE_RESULT
__ledf2(fp_t a, fp_t b) {
    
    const srep_t aInt = toRep(a);
    const srep_t bInt = toRep(b);
    const rep_t aAbs = aInt & absMask;
    const rep_t bAbs = bInt & absMask;
    
    // If either a or b is NaN, they are unordered.
    if (aAbs > infRep || bAbs > infRep) return LE_UNORDERED;
    
    // If a and b are both zeros, they are equal.
    if ((aAbs | bAbs) == 0) return LE_EQUAL;
    
    // If at least one of a and b is positive, we get the same result comparing
    // a and b as signed integers as we would with a floating-point compare.
    if ((aInt & bInt) >= 0) {
        if (aInt < bInt) return LE_LESS;
        else if (aInt == bInt) return LE_EQUAL;
        else return LE_GREATER;
    }
    
    // Otherwise, both are negative, so we need to flip the sense of the
    // comparison to get the correct result.  (This assumes a twos- or ones-
    // complement integer representation; if integers are represented in a
    // sign-magnitude representation, then this flip is incorrect).
    else {
        if (aInt > bInt) return LE_LESS;
        else if (aInt == bInt) return LE_EQUAL;
        else return LE_GREATER;
    }
}

#if defined(__ELF__)
// Alias for libgcc compatibility
FNALIAS(__cmpdf2, __ledf2);
#endif

enum GE_RESULT {
    GE_LESS      = -1,
    GE_EQUAL     =  0,
    GE_GREATER   =  1,
    GE_UNORDERED = -1   // Note: different from LE_UNORDERED
};

COMPILER_RT_ABI enum GE_RESULT
__gedf2(fp_t a, fp_t b) {
    
    const srep_t aInt = toRep(a);
    const srep_t bInt = toRep(b);
    const rep_t aAbs = aInt & absMask;
    const rep_t bAbs = bInt & absMask;
    
    if (aAbs > infRep || bAbs > infRep) return GE_UNORDERED;
    if ((aAbs | bAbs) == 0) return GE_EQUAL;
    if ((aInt & bInt) >= 0) {
        if (aInt < bInt) return GE_LESS;
        else if (aInt == bInt) return GE_EQUAL;
        else return GE_GREATER;
    } else {
        if (aInt > bInt) return GE_LESS;
        else if (aInt == bInt) return GE_EQUAL;
        else return GE_GREATER;
    }
}

COMPILER_RT_ABI int
__unorddf2(fp_t a, fp_t b) {
    const rep_t aAbs = toRep(a) & absMask;
    const rep_t bAbs = toRep(b) & absMask;
    return aAbs > infRep || bAbs > infRep;
}

// The following are alternative names for the preceding routines.

COMPILER_RT_ABI enum LE_RESULT
__eqdf2(fp_t a, fp_t b) {
    return __ledf2(a, b);
}

COMPILER_RT_ABI enum LE_RESULT
__ltdf2(fp_t a, fp_t b) {
    return __ledf2(a, b);
}

COMPILER_RT_ABI enum LE_RESULT
__nedf2(fp_t a, fp_t b) {
    return __ledf2(a, b);
}

COMPILER_RT_ABI enum GE_RESULT
__gtdf2(fp_t a, fp_t b) {
    return __gedf2(a, b);
}

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI int __aeabi_dcmpun(fp_t a, fp_t b) {
  return __unorddf2(a, b);
}
#else
AEABI_RTABI int __aeabi_dcmpun(fp_t a, fp_t b) COMPILER_RT_ALIAS(__unorddf2);
#endif
#endif
