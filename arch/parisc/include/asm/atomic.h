/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2000 Philipp Rumpf <prumpf@tux.org>
 * Copyright (C) 2006 Kyle McMartin <kyle@parisc-linux.org>
 */

#ifndef _ASM_PARISC_ATOMIC_H_
#define _ASM_PARISC_ATOMIC_H_

#include <linux/types.h>
#include <asm/cmpxchg.h>
#include <asm/barrier.h>

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 *
 * And probably incredibly slow on parisc.  OTOH, we don't
 * have to write any serious assembly.   prumpf
 */

#ifdef CONFIG_SMP
#include <asm/spinlock.h>
#include <asm/cache.h>		/* we use L1_CACHE_BYTES */

/* Use an array of spinlocks for our atomic_ts.
 * Hash function to index into a different SPINLOCK.
 * Since "a" is usually an address, use one spinlock per cacheline.
 */
#  define ATOMIC_HASH_SIZE 4
#  define ATOMIC_HASH(a) (&(__atomic_hash[ (((unsigned long) (a))/L1_CACHE_BYTES) & (ATOMIC_HASH_SIZE-1) ]))

extern arch_spinlock_t __atomic_hash[ATOMIC_HASH_SIZE] __lock_aligned;

/* Can't use raw_spin_lock_irq because of #include problems, so
 * this is the substitute */
#define _atomic_spin_lock_irqsave(l,f) do {	\
	arch_spinlock_t *s = ATOMIC_HASH(l);		\
	local_irq_save(f);			\
	arch_spin_lock(s);			\
} while(0)

#define _atomic_spin_unlock_irqrestore(l,f) do {	\
	arch_spinlock_t *s = ATOMIC_HASH(l);			\
	arch_spin_unlock(s);				\
	local_irq_restore(f);				\
} while(0)


#else
#  define _atomic_spin_lock_irqsave(l,f) do { local_irq_save(f); } while (0)
#  define _atomic_spin_unlock_irqrestore(l,f) do { local_irq_restore(f); } while (0)
#endif

/*
 * Note that we need not lock read accesses - aligned word writes/reads
 * are atomic, so a reader never sees inconsistent values.
 */

static __inline__ void atomic_set(atomic_t *v, int i)
{
	unsigned long flags;
	_atomic_spin_lock_irqsave(v, flags);

	v->counter = i;

	_atomic_spin_unlock_irqrestore(v, flags);
}

#define atomic_set_release(v, i)	atomic_set((v), (i))

static __inline__ int atomic_read(const atomic_t *v)
{
	return READ_ONCE((v)->counter);
}

/* exported interface */
#define atomic_cmpxchg(v, o, n) (cmpxchg(&((v)->counter), (o), (n)))
#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

#define ATOMIC_OP(op, c_op)						\
static __inline__ void atomic_##op(int i, atomic_t *v)			\
{									\
	unsigned long flags;						\
									\
	_atomic_spin_lock_irqsave(v, flags);				\
	v->counter c_op i;						\
	_atomic_spin_unlock_irqrestore(v, flags);			\
}									\

#define ATOMIC_OP_RETURN(op, c_op)					\
static __inline__ int atomic_##op##_return(int i, atomic_t *v)		\
{									\
	unsigned long flags;						\
	int ret;							\
									\
	_atomic_spin_lock_irqsave(v, flags);				\
	ret = (v->counter c_op i);					\
	_atomic_spin_unlock_irqrestore(v, flags);			\
									\
	return ret;							\
}

#define ATOMIC_FETCH_OP(op, c_op)					\
static __inline__ int atomic_fetch_##op(int i, atomic_t *v)		\
{									\
	unsigned long flags;						\
	int ret;							\
									\
	_atomic_spin_lock_irqsave(v, flags);				\
	ret = v->counter;						\
	v->counter c_op i;						\
	_atomic_spin_unlock_irqrestore(v, flags);			\
									\
	return ret;							\
}

#define ATOMIC_OPS(op, c_op)						\
	ATOMIC_OP(op, c_op)						\
	ATOMIC_OP_RETURN(op, c_op)					\
	ATOMIC_FETCH_OP(op, c_op)

ATOMIC_OPS(add, +=)
ATOMIC_OPS(sub, -=)

#undef ATOMIC_OPS
#define ATOMIC_OPS(op, c_op)						\
	ATOMIC_OP(op, c_op)						\
	ATOMIC_FETCH_OP(op, c_op)

ATOMIC_OPS(and, &=)
ATOMIC_OPS(or, |=)
ATOMIC_OPS(xor, ^=)

#undef ATOMIC_OPS
#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

#define ATOMIC_INIT(i)	{ (i) }

#ifdef CONFIG_64BIT

#define ATOMIC64_INIT(i) { (i) }

#define ATOMIC64_OP(op, c_op)						\
static __inline__ void atomic64_##op(s64 i, atomic64_t *v)		\
{									\
	unsigned long flags;						\
									\
	_atomic_spin_lock_irqsave(v, flags);				\
	v->counter c_op i;						\
	_atomic_spin_unlock_irqrestore(v, flags);			\
}									\

#define ATOMIC64_OP_RETURN(op, c_op)					\
static __inline__ s64 atomic64_##op##_return(s64 i, atomic64_t *v)	\
{									\
	unsigned long flags;						\
	s64 ret;							\
									\
	_atomic_spin_lock_irqsave(v, flags);				\
	ret = (v->counter c_op i);					\
	_atomic_spin_unlock_irqrestore(v, flags);			\
									\
	return ret;							\
}

#define ATOMIC64_FETCH_OP(op, c_op)					\
static __inline__ s64 atomic64_fetch_##op(s64 i, atomic64_t *v)		\
{									\
	unsigned long flags;						\
	s64 ret;							\
									\
	_atomic_spin_lock_irqsave(v, flags);				\
	ret = v->counter;						\
	v->counter c_op i;						\
	_atomic_spin_unlock_irqrestore(v, flags);			\
									\
	return ret;							\
}

#define ATOMIC64_OPS(op, c_op)						\
	ATOMIC64_OP(op, c_op)						\
	ATOMIC64_OP_RETURN(op, c_op)					\
	ATOMIC64_FETCH_OP(op, c_op)

ATOMIC64_OPS(add, +=)
ATOMIC64_OPS(sub, -=)

#undef ATOMIC64_OPS
#define ATOMIC64_OPS(op, c_op)						\
	ATOMIC64_OP(op, c_op)						\
	ATOMIC64_FETCH_OP(op, c_op)

ATOMIC64_OPS(and, &=)
ATOMIC64_OPS(or, |=)
ATOMIC64_OPS(xor, ^=)

#undef ATOMIC64_OPS
#undef ATOMIC64_FETCH_OP
#undef ATOMIC64_OP_RETURN
#undef ATOMIC64_OP

static __inline__ void
atomic64_set(atomic64_t *v, s64 i)
{
	unsigned long flags;
	_atomic_spin_lock_irqsave(v, flags);

	v->counter = i;

	_atomic_spin_unlock_irqrestore(v, flags);
}

static __inline__ s64
atomic64_read(const atomic64_t *v)
{
	return READ_ONCE((v)->counter);
}

/* exported interface */
#define atomic64_cmpxchg(v, o, n) \
	((__typeof__((v)->counter))cmpxchg(&((v)->counter), (o), (n)))
#define atomic64_xchg(v, new) (xchg(&((v)->counter), new))

#endif /* !CONFIG_64BIT */


#endif /* _ASM_PARISC_ATOMIC_H_ */
