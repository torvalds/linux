/*
 * Copyright 2004-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __ARCH_BLACKFIN_ATOMIC__
#define __ARCH_BLACKFIN_ATOMIC__

#include <asm/cmpxchg.h>

#ifdef CONFIG_SMP

#include <asm/barrier.h>
#include <linux/linkage.h>
#include <linux/types.h>

asmlinkage int __raw_uncached_fetch_asm(const volatile int *ptr);
asmlinkage int __raw_atomic_add_asm(volatile int *ptr, int value);
asmlinkage int __raw_atomic_xadd_asm(volatile int *ptr, int value);

asmlinkage int __raw_atomic_and_asm(volatile int *ptr, int value);
asmlinkage int __raw_atomic_or_asm(volatile int *ptr, int value);
asmlinkage int __raw_atomic_xor_asm(volatile int *ptr, int value);
asmlinkage int __raw_atomic_test_asm(const volatile int *ptr, int value);

#define atomic_read(v) __raw_uncached_fetch_asm(&(v)->counter)

#define atomic_add_return(i, v) __raw_atomic_add_asm(&(v)->counter, i)
#define atomic_sub_return(i, v) __raw_atomic_add_asm(&(v)->counter, -(i))

#define atomic_fetch_add(i, v) __raw_atomic_xadd_asm(&(v)->counter, i)
#define atomic_fetch_sub(i, v) __raw_atomic_xadd_asm(&(v)->counter, -(i))

#define atomic_or(i, v)  (void)__raw_atomic_or_asm(&(v)->counter, i)
#define atomic_and(i, v) (void)__raw_atomic_and_asm(&(v)->counter, i)
#define atomic_xor(i, v) (void)__raw_atomic_xor_asm(&(v)->counter, i)

#define atomic_fetch_or(i, v)  __raw_atomic_or_asm(&(v)->counter, i)
#define atomic_fetch_and(i, v) __raw_atomic_and_asm(&(v)->counter, i)
#define atomic_fetch_xor(i, v) __raw_atomic_xor_asm(&(v)->counter, i)

#endif

#include <asm-generic/atomic.h>

#endif
