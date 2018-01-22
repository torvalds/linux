/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __ASM_PARISC_BITSPERLONG_H
#define __ASM_PARISC_BITSPERLONG_H

#if defined(__LP64__)
#define __BITS_PER_LONG 64
#else
#define __BITS_PER_LONG 32
#endif

#include <asm-generic/bitsperlong.h>

#endif /* __ASM_PARISC_BITSPERLONG_H */
