/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_TYPES_H
#define _UAPI_ASM_TYPES_H

#include <asm-generic/int-ll64.h>

/*
 * The C99 types uintXX_t that are usually defined in 'stdint.h' are not as
 * unambiguous on ARM as you would expect. For the types below, there is a
 * difference on ARM between GCC built for bare metal ARM, GCC built for glibc
 * and the kernel itself, which results in build errors if you try to build with
 * -ffreestanding and include 'stdint.h' (such as when you include 'arm_neon.h'
 * in order to use NEON intrinsics)
 *
 * As the typedefs for these types in 'stdint.h' are based on builtin defines
 * supplied by GCC, we can tweak these to align with the kernel's idea of those
 * types, so 'linux/types.h' and 'stdint.h' can be safely included from the same
 * source file (provided that -ffreestanding is used).
 *
 *                    int32_t         uint32_t               uintptr_t
 * bare metal GCC     long            unsigned long          unsigned int
 * glibc GCC          int             unsigned int           unsigned int
 * kernel             int             unsigned int           unsigned long
 */

#ifdef __INT32_TYPE__
#undef __INT32_TYPE__
#define __INT32_TYPE__		int
#endif

#ifdef __UINT32_TYPE__
#undef __UINT32_TYPE__
#define __UINT32_TYPE__	unsigned int
#endif

#ifdef __UINTPTR_TYPE__
#undef __UINTPTR_TYPE__
#define __UINTPTR_TYPE__	unsigned long
#endif

#endif /* _UAPI_ASM_TYPES_H */
