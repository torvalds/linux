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
#include <asm/futex.h>
#include <arch/chip.h>

/* See <asm/atomic_32.h> */
#if ATOMIC_LOCKS_FOUND_VIA_TABLE()

/*
 * A block of memory containing locks for atomic ops. Each instance of this
 * struct will be homed on a different CPU.
 */
struct atomic_locks_on_cpu {
	int lock[ATOMIC_HASH_L2_SIZE];
} __attribute__((aligned(ATOMIC_HASH_L2_SIZE * 4)));

static DEFINE_PER_CPU(struct atomic_locks_on_cpu, atomic_lock_pool);

/* The locks we'll use until __init_atomic_per_cpu is called. */
static struct atomic_locks_on_cpu __initdata initial_atomic_locks;

/* Hash into this vector to get a pointer to lock for the given atomic. */
struct atomic_locks_on_cpu *atomic_lock_ptr[ATOMIC_HASH_L1_SIZE]
	__write_once = {
	[0 ... ATOMIC_HASH_L1_SIZE-1] (&initial_atomic_locks)
};

#else /* ATOMIC_LOCKS_FOUND_VIA_TABLE() */

/* This page is remapped on startup to be hash-for-home. */
int atomic_locks[PAGE_SIZE / sizeof(int)] __page_aligned_bss;

#endif /* ATOMIC_LOCKS_FOUND_VIA_TABLE() */

static inline int *__atomic_hashed_lock(volatile void *v)
{
	/* NOTE: this code must match "sys_cmpxchg" in kernel/intvec_32.S */
#if ATOMIC_LOCKS_FOUND_VIA_TABLE()
	unsigned long i =
		(unsigned long) v & ((PAGE_SIZE-1) & -sizeof(long long));
	unsigned long n = __insn_crc32_32(0, i);

	/* Grab high bits for L1 index. */
	unsigned long l1_index = n >> ((sizeof(n) * 8) - ATOMIC_HASH_L1_SHIFT);
	/* Grab low bits for L2 index. */
	unsigned long l2_index = n & (ATOMIC_HASH_L2_SIZE - 1);

	return &atomic_lock_ptr[l1_index]->lock[l2_index];
#else
	/*
	 * Use bits [3, 3 + ATOMIC_HASH_SHIFT) as the lock index.
	 * Using mm works here because atomic_locks is page aligned.
	 */
	unsigned long ptr = __insn_mm((unsigned long)v >> 1,
				      (unsigned long)atomic_locks,
				      2, (ATOMIC_HASH_SHIFT + 2) - 1);
	return (int *)ptr;
#endif
}

#ifdef CONFIG_SMP
/* Return whether the passed pointer is a valid atomic lock pointer. */
static int is_atomic_lock(int *p)
{
#if ATOMIC_LOCKS_FOUND_VIA_TABLE()
	int i;
	for (i = 0; i < ATOMIC_HASH_L1_SIZE; ++i) {

		if (p >= &atomic_lock_ptr[i]->lock[0] &&
		    p < &atomic_lock_ptr[i]->lock[ATOMIC_HASH_L2_SIZE]) {
			return 1;
		}
	}
	return 0;
#else
	return p >= &atomic_locks[0] && p < &atomic_locks[ATOMIC_HASH_SIZE];
#endif
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


static inline int *__futex_setup(int __user *v)
{
	/*
	 * Issue a prefetch to the counter to bring it into cache.
	 * As for __atomic_setup, but we can't do a read into the L1
	 * since it might fault; instead we do a prefetch into the L2.
	 */
	__insn_prefetch(v);
	return __atomic_hashed_lock((int __force *)v);
}

struct __get_user futex_set(u32 __user *v, int i)
{
	return __atomic_xchg((int __force *)v, __futex_setup(v), i);
}

struct __get_user futex_add(u32 __user *v, int n)
{
	return __atomic_xchg_add((int __force *)v, __futex_setup(v), n);
}

struct __get_user futex_or(u32 __user *v, int n)
{
	return __atomic_or((int __force *)v, __futex_setup(v), n);
}

struct __get_user futex_andn(u32 __user *v, int n)
{
	return __atomic_andn((int __force *)v, __futex_setup(v), n);
}

struct __get_user futex_xor(u32 __user *v, int n)
{
	return __atomic_xor((int __force *)v, __futex_setup(v), n);
}

struct __get_user futex_cmpxchg(u32 __user *v, int o, int n)
{
	return __atomic_cmpxchg((int __force *)v, __futex_setup(v), o, n);
}

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


#if CHIP_HAS_CBOX_HOME_MAP()
static int __init noatomichash(char *str)
{
	pr_warning("noatomichash is deprecated.\n");
	return 1;
}
__setup("noatomichash", noatomichash);
#endif

void __init __init_atomic_per_cpu(void)
{
#if ATOMIC_LOCKS_FOUND_VIA_TABLE()

	unsigned int i;
	int actual_cpu;

	/*
	 * Before this is called from setup, we just have one lock for
	 * all atomic objects/operations.  Here we replace the
	 * elements of atomic_lock_ptr so that they point at per_cpu
	 * integers.  This seemingly over-complex approach stems from
	 * the fact that DEFINE_PER_CPU defines an entry for each cpu
	 * in the grid, not each cpu from 0..ATOMIC_HASH_SIZE-1.  But
	 * for efficient hashing of atomics to their locks we want a
	 * compile time constant power of 2 for the size of this
	 * table, so we use ATOMIC_HASH_SIZE.
	 *
	 * Here we populate atomic_lock_ptr from the per cpu
	 * atomic_lock_pool, interspersing by actual cpu so that
	 * subsequent elements are homed on consecutive cpus.
	 */

	actual_cpu = cpumask_first(cpu_possible_mask);

	for (i = 0; i < ATOMIC_HASH_L1_SIZE; ++i) {
		/*
		 * Preincrement to slightly bias against using cpu 0,
		 * which has plenty of stuff homed on it already.
		 */
		actual_cpu = cpumask_next(actual_cpu, cpu_possible_mask);
		if (actual_cpu >= nr_cpu_ids)
			actual_cpu = cpumask_first(cpu_possible_mask);

		atomic_lock_ptr[i] = &per_cpu(atomic_lock_pool, actual_cpu);
	}

#else /* ATOMIC_LOCKS_FOUND_VIA_TABLE() */

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

#endif /* ATOMIC_LOCKS_FOUND_VIA_TABLE() */

	/* The futex code makes this assumption, so we validate it here. */
	BUILD_BUG_ON(sizeof(atomic_t) != sizeof(int));
}
