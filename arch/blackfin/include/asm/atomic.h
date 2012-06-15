/*
 * Copyright 2004-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __ARCH_BLACKFIN_ATOMIC__
#define __ARCH_BLACKFIN_ATOMIC__

#include <asm/cmpxchg.h>

#ifdef CONFIG_SMP

#include <linux/linkage.h>

asmlinkage int __raw_uncached_fetch_asm(const volatile int *ptr);
asmlinkage int __raw_atomic_update_asm(volatile int *ptr, int value);
asmlinkage int __raw_atomic_clear_asm(volatile int *ptr, int value);
asmlinkage int __raw_atomic_set_asm(volatile int *ptr, int value);
asmlinkage int __raw_atomic_xor_asm(volatile int *ptr, int value);
asmlinkage int __raw_atomic_test_asm(const volatile int *ptr, int value);

#define atomic_read(v) __raw_uncached_fetch_asm(&(v)->counter)

#define atomic_add_return(i, v) __raw_atomic_update_asm(&(v)->counter, i)
#define atomic_sub_return(i, v) __raw_atomic_update_asm(&(v)->counter, -(i))

#define atomic_clear_mask(m, v) __raw_atomic_clear_asm(&(v)->counter, m)
#define atomic_set_mask(m, v)   __raw_atomic_set_asm(&(v)->counter, m)

#endif

#include <asm-generic/atomic.h>

#endif
