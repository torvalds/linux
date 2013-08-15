/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/cache.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/atomic.h>
#include <arch/chip.h>

/* This page is remapped on startup to be hash-for-home. */
int atomic_locks[PAGE_SIZE / sizeof(int)] __page_aligned_bss;

int *__atomic_hashed_lock(volatile void *v)
{
	/* NOTE: this code must match "sys_cmpxchg" in kernel/intvec_32.S */
	/*
	 * Use bits [3, 3 + ATOMIC_HASH_SHIFT) as the lock index.
	 * Using mm works here because atomic_locks is page aligned.
	 */
	unsigned long ptr = __insn_mm((unsigned long)v >> 1,
				      (unsigned long)atomic_locks,
				      2, (ATOMIC_HASH_SHIFT + 2) - 1);
	return (int *)ptr;
}

#ifdef CONFIG_SMP
/* Return whether the passed pointer is a valid atomic lock pointer. */
static int is_atomic_lock(int *p)
{
	return p >= &atomic_locks[0] && p < &atomic_locks[ATOMIC_HASH_SIZE];
}

void __atomic_fault_unlock(int *irqlock_word)
{
	BUG_ON(!is_atomic_lock(irqlock_word));
	BUG_ON(*irqlock_word != 1);
	*irqlock_word = 0;
}

#endif /* CONFIG_SMP */

static inline int *__atomic_setup(volatile void *v)
{
	/* Issue a load to the target to bring it into cache. */
	*(volatile int *)v;
	return __atomic_hashed_lock(v);
}

int _atomic_xchg(atomic_t *v, int n)
{
	return __atomic_xchg(&v->counter, __atomic_setup(v), n).val;
}
EXPORT_SYMBOL(_atomic_xchg);

int _atomic_xchg_add(atomic_t *v, int i)
{
	return __atomic_xchg_add(&v->counter, __atomic_setup(v), i).val;
}
EXPORT_SYMBOL(_atomic_xchg_add);

int _atomic_xchg_add_unless(atomic_t *v, int a, int u)
{
	/*
	 * Note: argument order is switched here since it is easier
	 * to use the first argument consistently as the "old value"
	 * in the assembly, as is done for _atomic_cmpxchg().
	 */
	return __atomic_xchg_add_unless(&v->counter, __atomic_setup(v), u, a)
		.val;
}
EXPORT_SYMBOL(_atomic_xchg_add_unless);

int _atomic_cmpxchg(atomic_t *v, int o, int n)
{
	return __atomic_cmpxchg(&v->counter, __atomic_setup(v), o, n).val;
}
EXPORT_SYMBOL(_atomic_cmpxchg);

unsigned long _atomic_or(volatile unsigned long *p, unsigned long mask)
{
	return __atomic_or((int *)p, __atomic_setup(p), mask).val;
}
EXPORT_SYMBOL(_atomic_or);

unsigned long _atomic_andn(volatile unsigned long *p, unsigned long mask)
{
	return __atomic_andn((int *)p, __atomic_setup(p), mask).val;
}
EXPORT_SYMBOL(_atomic_andn);

unsigned long _atomic_xor(volatile unsigned long *p, unsigned long mask)
{
	return __atomic_xor((int *)p, __atomic_setup(p), mask).val;
}
EXPORT_SYMBOL(_atomic_xor);


u64 _atomic64_xchg(atomic64_t *v, u64 n)
{
	return __atomic64_xchg(&v->counter, __atomic_setup(v), n);
}
EXPORT_SYMBOL(_atomic64_xchg);

u64 _atomic64_xchg_add(atomic64_t *v, u64 i)
{
	return __atomic64_xchg_add(&v->counter, __atomic_setup(v), i);
}
EXPORT_SYMBOL(_atomic64_xchg_add);

u64 _atomic64_xchg_add_unless(atomic64_t *v, u64 a, u64 u)
{
	/*
	 * Note: argument order is switched here since it is easier
	 * to use the first argument consistently as the "old value"
	 * in the assembly, as is done for _atomic_cmpxchg().
	 */
	return __atomic64_xchg_add_unless(&v->counter, __atomic_setup(v),
					  u, a);
}
EXPORT_SYMBOL(_atomic64_xchg_add_unless);

u64 _atomic64_cmpxchg(atomic64_t *v, u64 o, u64 n)
{
	return __atomic64_cmpxchg(&v->counter, __atomic_setup(v), o, n);
}
EXPORT_SYMBOL(_atomic64_cmpxchg);


/*
 * If any of the atomic or futex routines hit a bad address (not in
 * the page tables at kernel PL) this routine is called.  The futex
 * routines are never used on kernel space, and the normal atomics and
 * bitops are never used on user space.  So a fault on kernel space
 * must be fatal, but a fault on userspace is a futex fault and we
 * need to return -EFAULT.  Note that the context this routine is
 * invoked in is the context of the "_atomic_xxx()" routines called
 * by the functions in this file.
 */
struct __get_user __atomic_bad_address(int __user *addr)
{
	if (unlikely(!access_ok(VERIFY_WRITE, addr, sizeof(int))))
		panic("Bad address used for kernel atomic op: %p\n", addr);
	return (struct __get_user) { .err = -EFAULT };
}


void __init __init_atomic_per_cpu(void)
{
	/* Validate power-of-two and "bigger than cpus" assumption */
	BUILD_BUG_ON(ATOMIC_HASH_SIZE & (ATOMIC_HASH_SIZE-1));
	BUG_ON(ATOMIC_HASH_SIZE < nr_cpu_ids);

	/*
	 * On TILEPro we prefer to use a single hash-for-home
	 * page, since this means atomic operations are less
	 * likely to encounter a TLB fault and thus should
	 * in general perform faster.  You may wish to disable
	 * this in situations where few hash-for-home tiles
	 * are configured.
	 */
	BUG_ON((unsigned long)atomic_locks % PAGE_SIZE != 0);

	/* The locks must all fit on one page. */
	BUILD_BUG_ON(ATOMIC_HASH_SIZE * sizeof(int) > PAGE_SIZE);

	/*
	 * We use the page offset of the atomic value's address as
	 * an index into atomic_locks, excluding the low 3 bits.
	 * That should not produce more indices than ATOMIC_HASH_SIZE.
	 */
	BUILD_BUG_ON((PAGE_SIZE >> 3) > ATOMIC_HASH_SIZE);
}
