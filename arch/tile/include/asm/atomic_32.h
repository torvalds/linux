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
 * Do not include directly; use <asm/atomic.h>.
 */

#ifndef _ASM_TILE_ATOMIC_32_H
#define _ASM_TILE_ATOMIC_32_H

#include <arch/chip.h>

#ifndef __ASSEMBLY__

/* Tile-specific routines to support <asm/atomic.h>. */
int _atomic_xchg(atomic_t *v, int n);
int _atomic_xchg_add(atomic_t *v, int i);
int _atomic_xchg_add_unless(atomic_t *v, int a, int u);
int _atomic_cmpxchg(atomic_t *v, int o, int n);

/**
 * atomic_xchg - atomically exchange contents of memory with a new value
 * @v: pointer of type atomic_t
 * @i: integer value to store in memory
 *
 * Atomically sets @v to @i and returns old @v
 */
static inline int atomic_xchg(atomic_t *v, int n)
{
	smp_mb();  /* barrier for proper semantics */
	return _atomic_xchg(v, n);
}

/**
 * atomic_cmpxchg - atomically exchange contents of memory if it matches
 * @v: pointer of type atomic_t
 * @o: old value that memory should have
 * @n: new value to write to memory if it matches
 *
 * Atomically checks if @v holds @o and replaces it with @n if so.
 * Returns the old value at @v.
 */
static inline int atomic_cmpxchg(atomic_t *v, int o, int n)
{
	smp_mb();  /* barrier for proper semantics */
	return _atomic_cmpxchg(v, o, n);
}

/**
 * atomic_add - add integer to atomic variable
 * @i: integer value to add
 * @v: pointer of type atomic_t
 *
 * Atomically adds @i to @v.
 */
static inline void atomic_add(int i, atomic_t *v)
{
	_atomic_xchg_add(v, i);
}

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
	return _atomic_xchg_add(v, i) + i;
}

/**
 * atomic_add_unless - add unless the number is already a given value
 * @v: pointer of type atomic_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, so long as @v was not already @u.
 * Returns non-zero if @v was not @u, and zero otherwise.
 */
static inline int atomic_add_unless(atomic_t *v, int a, int u)
{
	smp_mb();  /* barrier for proper semantics */
	return _atomic_xchg_add_unless(v, a, u) != u;
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
	_atomic_xchg(v, n);
}

#define xchg(ptr, x) ((typeof(*(ptr))) \
  ((sizeof(*(ptr)) == sizeof(atomic_t)) ? \
   atomic_xchg((atomic_t *)(ptr), (long)(x)) : \
   __xchg_called_with_bad_pointer()))

#define cmpxchg(ptr, o, n) ((typeof(*(ptr))) \
  ((sizeof(*(ptr)) == sizeof(atomic_t)) ? \
   atomic_cmpxchg((atomic_t *)(ptr), (long)(o), (long)(n)) : \
   __cmpxchg_called_with_bad_pointer()))

/* A 64bit atomic type */

typedef struct {
	u64 __aligned(8) counter;
} atomic64_t;

#define ATOMIC64_INIT(val) { (val) }

u64 _atomic64_xchg(atomic64_t *v, u64 n);
u64 _atomic64_xchg_add(atomic64_t *v, u64 i);
u64 _atomic64_xchg_add_unless(atomic64_t *v, u64 a, u64 u);
u64 _atomic64_cmpxchg(atomic64_t *v, u64 o, u64 n);

/**
 * atomic64_read - read atomic variable
 * @v: pointer of type atomic64_t
 *
 * Atomically reads the value of @v.
 */
static inline u64 atomic64_read(const atomic64_t *v)
{
	/*
	 * Requires an atomic op to read both 32-bit parts consistently.
	 * Casting away const is safe since the atomic support routines
	 * do not write to memory if the value has not been modified.
	 */
	return _atomic64_xchg_add((atomic64_t *)v, 0);
}

/**
 * atomic64_xchg - atomically exchange contents of memory with a new value
 * @v: pointer of type atomic64_t
 * @i: integer value to store in memory
 *
 * Atomically sets @v to @i and returns old @v
 */
static inline u64 atomic64_xchg(atomic64_t *v, u64 n)
{
	smp_mb();  /* barrier for proper semantics */
	return _atomic64_xchg(v, n);
}

/**
 * atomic64_cmpxchg - atomically exchange contents of memory if it matches
 * @v: pointer of type atomic64_t
 * @o: old value that memory should have
 * @n: new value to write to memory if it matches
 *
 * Atomically checks if @v holds @o and replaces it with @n if so.
 * Returns the old value at @v.
 */
static inline u64 atomic64_cmpxchg(atomic64_t *v, u64 o, u64 n)
{
	smp_mb();  /* barrier for proper semantics */
	return _atomic64_cmpxchg(v, o, n);
}

/**
 * atomic64_add - add integer to atomic variable
 * @i: integer value to add
 * @v: pointer of type atomic64_t
 *
 * Atomically adds @i to @v.
 */
static inline void atomic64_add(u64 i, atomic64_t *v)
{
	_atomic64_xchg_add(v, i);
}

/**
 * atomic64_add_return - add integer and return
 * @v: pointer of type atomic64_t
 * @i: integer value to add
 *
 * Atomically adds @i to @v and returns @i + @v
 */
static inline u64 atomic64_add_return(u64 i, atomic64_t *v)
{
	smp_mb();  /* barrier for proper semantics */
	return _atomic64_xchg_add(v, i) + i;
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
static inline u64 atomic64_add_unless(atomic64_t *v, u64 a, u64 u)
{
	smp_mb();  /* barrier for proper semantics */
	return _atomic64_xchg_add_unless(v, a, u) != u;
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
static inline void atomic64_set(atomic64_t *v, u64 n)
{
	_atomic64_xchg(v, n);
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

/*
 * We need to barrier before modifying the word, since the _atomic_xxx()
 * routines just tns the lock and then read/modify/write of the word.
 * But after the word is updated, the routine issues an "mf" before returning,
 * and since it's a function call, we don't even need a compiler barrier.
 */
#define smp_mb__before_atomic_dec()	smp_mb()
#define smp_mb__before_atomic_inc()	smp_mb()
#define smp_mb__after_atomic_dec()	do { } while (0)
#define smp_mb__after_atomic_inc()	do { } while (0)

#endif /* !__ASSEMBLY__ */

/*
 * Internal definitions only beyond this point.
 */

#define ATOMIC_LOCKS_FOUND_VIA_TABLE() \
  (!CHIP_HAS_CBOX_HOME_MAP() && defined(CONFIG_SMP))

#if ATOMIC_LOCKS_FOUND_VIA_TABLE()

/* Number of entries in atomic_lock_ptr[]. */
#define ATOMIC_HASH_L1_SHIFT 6
#define ATOMIC_HASH_L1_SIZE (1 << ATOMIC_HASH_L1_SHIFT)

/* Number of locks in each struct pointed to by atomic_lock_ptr[]. */
#define ATOMIC_HASH_L2_SHIFT (CHIP_L2_LOG_LINE_SIZE() - 2)
#define ATOMIC_HASH_L2_SIZE (1 << ATOMIC_HASH_L2_SHIFT)

#else /* ATOMIC_LOCKS_FOUND_VIA_TABLE() */

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

#endif /* ATOMIC_LOCKS_FOUND_VIA_TABLE() */

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

/* Private helper routines in lib/atomic_asm_32.S */
extern struct __get_user __atomic_cmpxchg(volatile int *p,
					  int *lock, int o, int n);
extern struct __get_user __atomic_xchg(volatile int *p, int *lock, int n);
extern struct __get_user __atomic_xchg_add(volatile int *p, int *lock, int n);
extern struct __get_user __atomic_xchg_add_unless(volatile int *p,
						  int *lock, int o, int n);
extern struct __get_user __atomic_or(volatile int *p, int *lock, int n);
extern struct __get_user __atomic_andn(volatile int *p, int *lock, int n);
extern struct __get_user __atomic_xor(volatile int *p, int *lock, int n);
extern u64 __atomic64_cmpxchg(volatile u64 *p, int *lock, u64 o, u64 n);
extern u64 __atomic64_xchg(volatile u64 *p, int *lock, u64 n);
extern u64 __atomic64_xchg_add(volatile u64 *p, int *lock, u64 n);
extern u64 __atomic64_xchg_add_unless(volatile u64 *p,
				      int *lock, u64 o, u64 n);

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_TILE_ATOMIC_32_H */
