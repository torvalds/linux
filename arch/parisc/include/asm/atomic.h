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

static __inline__ int atomic_read(const atomic_t *v)
{
	return ACCESS_ONCE((v)->counter);
}

/* exported interface */
#define atomic_cmpxchg(v, o, n) (cmpxchg(&((v)->counter), (o), (n)))
#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

/**
 * __atomic_add_unless - add unless the number is a given value
 * @v: pointer of type atomic_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, so long as it was not @u.
 * Returns the old value of @v.
 */
static __inline__ int __atomic_add_unless(atomic_t *v, int a, int u)
{
	int c, old;
	c = atomic_read(v);
	for (;;) {
		if (unlikely(c == (u)))
			break;
		old = atomic_cmpxchg((v), c, c + (a));
		if (likely(old == c))
			break;
		c = old;
	}
	return c;
}

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

#define ATOMIC_OPS(op, c_op) ATOMIC_OP(op, c_op) ATOMIC_OP_RETURN(op, c_op)

ATOMIC_OPS(add, +=)
ATOMIC_OPS(sub, -=)

ATOMIC_OP(and, &=)
ATOMIC_OP(or, |=)
ATOMIC_OP(xor, ^=)

#undef ATOMIC_OPS
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

#define atomic_inc(v)	(atomic_add(   1,(v)))
#define atomic_dec(v)	(atomic_add(  -1,(v)))

#define atomic_inc_return(v)	(atomic_add_return(   1,(v)))
#define atomic_dec_return(v)	(atomic_add_return(  -1,(v)))

#define atomic_add_negative(a, v)	(atomic_add_return((a), (v)) < 0)

/*
 * atomic_inc_and_test - increment and test
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
#define atomic_inc_and_test(v) (atomic_inc_return(v) == 0)

#define atomic_dec_and_test(v)	(atomic_dec_return(v) == 0)

#define atomic_sub_and_test(i,v)	(atomic_sub_return((i),(v)) == 0)

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

#define ATOMIC64_OPS(op, c_op) ATOMIC64_OP(op, c_op) ATOMIC64_OP_RETURN(op, c_op)

ATOMIC64_OPS(add, +=)
ATOMIC64_OPS(sub, -=)
ATOMIC64_OP(and, &=)
ATOMIC64_OP(or, |=)
ATOMIC64_OP(xor, ^=)

#undef ATOMIC64_OPS
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
	return ACCESS_ONCE((v)->counter);
}

#define atomic64_inc(v)		(atomic64_add(   1,(v)))
#define atomic64_dec(v)		(atomic64_add(  -1,(v)))

#define atomic64_inc_return(v)		(atomic64_add_return(   1,(v)))
#define atomic64_dec_return(v)		(atomic64_add_return(  -1,(v)))

#define atomic64_add_negative(a, v)	(atomic64_add_return((a), (v)) < 0)

#define atomic64_inc_and_test(v) 	(atomic64_inc_return(v) == 0)
#define atomic64_dec_and_test(v)	(atomic64_dec_return(v) == 0)
#define atomic64_sub_and_test(i,v)	(atomic64_sub_return((i),(v)) == 0)

/* exported interface */
#define atomic64_cmpxchg(v, o, n) \
	((__typeof__((v)->counter))cmpxchg(&((v)->counter), (o), (n)))
#define atomic64_xchg(v, new) (xchg(&((v)->counter), new))

/**
 * atomic64_add_unless - add unless the number is a given value
 * @v: pointer of type atomic64_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, so long as it was not @u.
 * Returns the old value of @v.
 */
static __inline__ int atomic64_add_unless(atomic64_t *v, long a, long u)
{
	long c, old;
	c = atomic64_read(v);
	for (;;) {
		if (unlikely(c == (u)))
			break;
		old = atomic64_cmpxchg((v), c, c + (a));
		if (likely(old == c))
			break;
		c = old;
	}
	return c != (u);
}

#define atomic64_inc_not_zero(v) atomic64_add_unless((v), 1, 0)

/*
 * atomic64_dec_if_positive - decrement by 1 if old value positive
 * @v: pointer of type atomic_t
 *
 * The function returns the old value of *v minus 1, even if
 * the atomic variable, v, was not decremented.
 */
static inline long atomic64_dec_if_positive(atomic64_t *v)
{
	long c, old, dec;
	c = atomic64_read(v);
	for (;;) {
		dec = c - 1;
		if (unlikely(dec < 0))
			break;
		old = atomic64_cmpxchg((v), c, dec);
		if (likely(old == c))
			break;
		c = old;
	}
	return dec;
}

#endif /* !CONFIG_64BIT */


#endif /* _ASM_PARISC_ATOMIC_H_ */
