/*
 * z_Windows_NT-586_util.cpp -- platform specific routines.
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#include "kmp.h"

#if (KMP_ARCH_X86 || KMP_ARCH_X86_64)
/* Only 32-bit "add-exchange" instruction on IA-32 architecture causes us to
   use compare_and_store for these routines */

kmp_int8 __kmp_test_then_or8(volatile kmp_int8 *p, kmp_int8 d) {
  kmp_int8 old_value, new_value;

  old_value = TCR_1(*p);
  new_value = old_value | d;

  while (!__kmp_compare_and_store8(p, old_value, new_value)) {
    KMP_CPU_PAUSE();
    old_value = TCR_1(*p);
    new_value = old_value | d;
  }
  return old_value;
}

kmp_int8 __kmp_test_then_and8(volatile kmp_int8 *p, kmp_int8 d) {
  kmp_int8 old_value, new_value;

  old_value = TCR_1(*p);
  new_value = old_value & d;

  while (!__kmp_compare_and_store8(p, old_value, new_value)) {
    KMP_CPU_PAUSE();
    old_value = TCR_1(*p);
    new_value = old_value & d;
  }
  return old_value;
}

kmp_uint32 __kmp_test_then_or32(volatile kmp_uint32 *p, kmp_uint32 d) {
  kmp_uint32 old_value, new_value;

  old_value = TCR_4(*p);
  new_value = old_value | d;

  while (!__kmp_compare_and_store32((volatile kmp_int32 *)p, old_value,
                                    new_value)) {
    KMP_CPU_PAUSE();
    old_value = TCR_4(*p);
    new_value = old_value | d;
  }
  return old_value;
}

kmp_uint32 __kmp_test_then_and32(volatile kmp_uint32 *p, kmp_uint32 d) {
  kmp_uint32 old_value, new_value;

  old_value = TCR_4(*p);
  new_value = old_value & d;

  while (!__kmp_compare_and_store32((volatile kmp_int32 *)p, old_value,
                                    new_value)) {
    KMP_CPU_PAUSE();
    old_value = TCR_4(*p);
    new_value = old_value & d;
  }
  return old_value;
}

kmp_int8 __kmp_test_then_add8(volatile kmp_int8 *p, kmp_int8 d) {
  kmp_int64 old_value, new_value;

  old_value = TCR_1(*p);
  new_value = old_value + d;
  while (!__kmp_compare_and_store8(p, old_value, new_value)) {
    KMP_CPU_PAUSE();
    old_value = TCR_1(*p);
    new_value = old_value + d;
  }
  return old_value;
}

#if KMP_ARCH_X86
kmp_int64 __kmp_test_then_add64(volatile kmp_int64 *p, kmp_int64 d) {
  kmp_int64 old_value, new_value;

  old_value = TCR_8(*p);
  new_value = old_value + d;
  while (!__kmp_compare_and_store64(p, old_value, new_value)) {
    KMP_CPU_PAUSE();
    old_value = TCR_8(*p);
    new_value = old_value + d;
  }
  return old_value;
}
#endif /* KMP_ARCH_X86 */

kmp_uint64 __kmp_test_then_or64(volatile kmp_uint64 *p, kmp_uint64 d) {
  kmp_uint64 old_value, new_value;

  old_value = TCR_8(*p);
  new_value = old_value | d;
  while (!__kmp_compare_and_store64((volatile kmp_int64 *)p, old_value,
                                    new_value)) {
    KMP_CPU_PAUSE();
    old_value = TCR_8(*p);
    new_value = old_value | d;
  }

  return old_value;
}

kmp_uint64 __kmp_test_then_and64(volatile kmp_uint64 *p, kmp_uint64 d) {
  kmp_uint64 old_value, new_value;

  old_value = TCR_8(*p);
  new_value = old_value & d;
  while (!__kmp_compare_and_store64((volatile kmp_int64 *)p, old_value,
                                    new_value)) {
    KMP_CPU_PAUSE();
    old_value = TCR_8(*p);
    new_value = old_value & d;
  }

  return old_value;
}

#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */
