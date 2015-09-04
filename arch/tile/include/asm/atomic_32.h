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
 *
 * Do not include directly; use <linux/atomic.h>.
 */

#ifndef _ASM_TILE_ATOMIC_32_H
#define _ASM_TILE_ATOMIC_32_H

#include <asm/barrier.h>
#include <arch/chip.h>

#ifndef __ASSEMBLY__

/**
 * atomic_add - add integer to atomic variable
 * @i: integer value to add
 * @v: pointer of type atomic_t
 *
 * Atomically adds @i to @v.
 */
static inline void atomic_add(int i, atomic_t *v)
{
	_atomic_xchg_add(&v->counter, i);
}

#define ATOMIC_OP(op)							\
unsigned long _atomic_##op(volatile unsigned long *p, unsigned long mask); \
static inline void atomic_##op(int i, atomic_t *v)			\
{									\
	_atomic_##op((unsigned long *)&v->counter, i);			\
}

ATOMIC_OP(and)
ATOMIC_OP(or)
ATOMIC_OP(xor)

#undef ATOMIC_OP

/**
 * atomic_add_return - add integer and return
 * @v: pointer of type atomic_t
 * @i: integer value to add
 *
 * Atomically adds @i to @v and returns @i + @v
 */
static inline int atomic_add_return(int i, atomic_t *v)
{
	smp_mb();  /* barrier for proper semantics */
	return _atomic_xchg_add(&v->counter, i) + i;
}

/**
 * __atomic_add_unless - add unless the number is already a given value
 * @v: pointer of type atomic_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, so long as @v was not already @u.
 * Returns the old value of @v.
 */
static inline int __atomic_add_unless(atomic_t *v, int a, int u)
{
	smp_mb();  /* barrier for proper semantics */
	return _atomic_xchg_add_unless(&v->counter, a, u);
}

/**
 * atomic_set - set atomic variable
 * @v: pointer of type atomic_t
 * @i: required value
 *
 * Atomically sets the value of @v to @i.
 *
 * atomic_set() can't be just a raw store, since it would be lost if it
 * fell between the load and store of one of the other atomic ops.
 */
static inline void atomic_set(atomic_t *v, int n)
{
	_atomic_xchg(&v->counter, n);
}

/* A 64bit atomic type */

typedef struct {
	long long counter;
} atomic64_t;

#define ATOMIC64_INIT(val) { (val) }

/**
 * atomic64_read - read atomic variable
 * @v: pointer of type atomic64_t
 *
 * Atomically reads the value of @v.
 */
static inline long long atomic64_read(const atomic64_t *v)
{
	/*
	 * Requires an atomic op to read both 32-bit parts consistently.
	 * Casting away const is safe since the atomic support routines
	 * do not write to memory if the value has not been modified.
	 */
	return _atomic64_xchg_add((long long *)&v->counter, 0);
}

/**
 * atomic64_add - add integer to atomic variable
 * @i: integer value to add
 * @v: pointer of type atomic64_t
 *
 * Atomically adds @i to @v.
 */
static inline void atomic64_add(long long i, atomic64_t *v)
{
	_atomic64_xchg_add(&v->counter, i);
}

#define ATOMIC64_OP(op)						\
long long _atomic64_##op(long long *v, long long n);		\
static inline void atomic64_##op(long long i, atomic64_t *v)	\
{								\
	_atomic64_##op(&v->counter, i);				\
}

ATOMIC64_OP(and)
ATOMIC64_OP(or)
ATOMIC64_OP(xor)

/**
 * atomic64_add_return - add integer and return
 * @v: pointer of type atomic64_t
 * @i: integer value to add
 *
 * Atomically adds @i to @v and returns @i + @v
 */
static inline long long atomic64_add_return(long long i, atomic64_t *v)
{
	smp_mb();  /* barrier for proper semantics */
	return _atomic64_xchg_add(&v->counter, i) + i;
}

/**
 * atomic64_add_unless - add unless the number is already a given value
 * @v: pointer of type atomic64_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, so long as @v was not already @u.
 * Returns non-zero if @v was not @u, and zero otherwise.
 */
static inline long long atomic64_add_unless(atomic64_t *v, long long a,
					long long u)
{
	smp_mb();  /* barrier for proper semantics */
	return _atomic64_xchg_add_unless(&v->counter, a, u) != u;
}

/**
 * atomic64_set - set atomic variable
 * @v: pointer of type atomic64_t
 * @i: required value
 *
 * Atomically sets the value of @v to @i.
 *
 * atomic64_set() can't be just a raw store, since it would be lost if it
 * fell between the load and store of one of the other atomic ops.
 */
static inline void atomic64_set(atomic64_t *v, long long n)
{
	_atomic64_xchg(&v->counter, n);
}

#define atomic64_add_negative(a, v)	(atomic64_add_return((a), (v)) < 0)
#define atomic64_inc(v)			atomic64_add(1LL, (v))
#define atomic64_inc_return(v)		atomic64_add_return(1LL, (v))
#define atomic64_inc_and_test(v)	(atomic64_inc_return(v) == 0)
#define atomic64_sub_return(i, v)	atomic64_add_return(-(i), (v))
#define atomic64_sub_and_test(a, v)	(atomic64_sub_return((a), (v)) == 0)
#define atomic64_sub(i, v)		atomic64_add(-(i), (v))
#define atomic64_dec(v)			atomic64_sub(1LL, (v))
#define atomic64_dec_return(v)		atomic64_sub_return(1LL, (v))
#define atomic64_dec_and_test(v)	(atomic64_dec_return((v)) == 0)
#define atomic64_inc_not_zero(v)	atomic64_add_unless((v), 1LL, 0LL)


#endif /* !__ASSEMBLY__ */

/*
 * Internal definitions only beyond this point.
 */

/*
 * Number of atomic locks in atomic_locks[]. Must be a power of two.
 * There is no reason for more than PAGE_SIZE / 8 entries, since that
 * is the maximum number of pointer bits we can use to index this.
 * And we cannot have more than PAGE_SIZE / 4, since this has to
 * fit on a single page and each entry takes 4 bytes.
 */
#define ATOMIC_HASH_SHIFT (PAGE_SHIFT - 3)
#define ATOMIC_HASH_SIZE (1 << ATOMIC_HASH_SHIFT)

#ifndef __ASSEMBLY__
extern int atomic_locks[];
#endif

/*
 * All the code that may fault while holding an atomic lock must
 * place the pointer to the lock in ATOMIC_LOCK_REG so the fault code
 * can correctly release and reacquire the lock.  Note that we
 * mention the register number in a comment in "lib/atomic_asm.S" to help
 * assembly coders from using this register by mistake, so if it
 * is changed here, change that comment as well.
 */
#define ATOMIC_LOCK_REG 20
#define ATOMIC_LOCK_REG_NAME r20

#ifndef __ASSEMBLY__
/* Called from setup to initialize a hash table to point to per_cpu locks. */
void __init_atomic_per_cpu(void);

#ifdef CONFIG_SMP
/* Support releasing the atomic lock in do_page_fault_ics(). */
void __atomic_fault_unlock(int *lock_ptr);
#endif

/* Return a pointer to the lock for the given address. */
int *__atomic_hashed_lock(volatile void *v);

/* Private helper routines in lib/atomic_asm_32.S */
struct __get_user {
	unsigned long val;
	int err;
};
extern struct __get_user __atomic_cmpxchg(volatile int *p,
					  int *lock, int o, int n);
extern struct __get_user __atomic_xchg(volatile int *p, int *lock, int n);
extern struct __get_user __atomic_xchg_add(volatile int *p, int *lock, int n);
extern struct __get_user __atomic_xchg_add_unless(volatile int *p,
						  int *lock, int o, int n);
extern struct __get_user __atomic_or(volatile int *p, int *lock, int n);
extern struct __get_user __atomic_and(volatile int *p, int *lock, int n);
extern struct __get_user __atomic_andn(volatile int *p, int *lock, int n);
extern struct __get_user __atomic_xor(volatile int *p, int *lock, int n);
extern long long __atomic64_cmpxchg(volatile long long *p, int *lock,
					long long o, long long n);
extern long long __atomic64_xchg(volatile long long *p, int *lock, long long n);
extern long long __atomic64_xchg_add(volatile long long *p, int *lock,
					long long n);
extern long long __atomic64_xchg_add_unless(volatile long long *p,
					int *lock, long long o, long long n);
extern long long __atomic64_and(volatile long long *p, int *lock, long long n);
extern long long __atomic64_or(volatile long long *p, int *lock, long long n);
extern long long __atomic64_xor(volatile long long *p, int *lock, long long n);

/* Return failure from the atomic wrappers. */
struct __get_user __atomic_bad_address(int __user *addr);

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_TILE_ATOMIC_32_H */
