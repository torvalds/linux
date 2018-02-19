/* SPDX-License-Identifier: GPL-2.0 */
/*
 * bitops.h: Bit string operations on the V9.
 *
 * Copyright 1996, 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_BITOPS_H
#define _SPARC64_BITOPS_H

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <linux/compiler.h>
#include <asm/byteorder.h>
#include <asm/barrier.h>

int test_and_set_bit(unsigned long nr, volatile unsigned long *addr);
int test_and_clear_bit(unsigned long nr, volatile unsigned long *addr);
int test_and_change_bit(unsigned long nr, volatile unsigned long *addr);
void set_bit(unsigned long nr, volatile unsigned long *addr);
void clear_bit(unsigned long nr, volatile unsigned long *addr);
void change_bit(unsigned long nr, volatile unsigned long *addr);

int fls(unsigned int word);
int __fls(unsigned long word);

#include <asm-generic/bitops/non-atomic.h>

#include <asm-generic/bitops/fls64.h>

#ifdef __KERNEL__

int ffs(int x);
unsigned long __ffs(unsigned long);

#include <asm-generic/bitops/ffz.h>
#include <asm-generic/bitops/sched.h>

/*
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */

unsigned long __arch_hweight64(__u64 w);
unsigned int __arch_hweight32(unsigned int w);
unsigned int __arch_hweight16(unsigned int w);
unsigned int __arch_hweight8(unsigned int w);

#include <asm-generic/bitops/const_hweight.h>
#include <asm-generic/bitops/lock.h>
#endif /* __KERNEL__ */

#include <asm-generic/bitops/find.h>

#ifdef __KERNEL__

#include <asm-generic/bitops/le.h>

#include <asm-generic/bitops/ext2-atomic-setbit.h>

#endif /* __KERNEL__ */

#endif /* defined(_SPARC64_BITOPS_H) */
