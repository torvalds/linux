/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI__ASM_H8300_BITS_PER_LONG
#define _UAPI__ASM_H8300_BITS_PER_LONG

#include <asm-generic/bitsperlong.h>

#if !defined(__ASSEMBLY__)
/* h8300-unknown-linux required long */
#define __kernel_size_t __kernel_size_t
typedef unsigned long	__kernel_size_t;
typedef long		__kernel_ssize_t;
typedef long		__kernel_ptrdiff_t;
#endif

#endif /* _UAPI__ASM_H8300_BITS_PER_LONG */
