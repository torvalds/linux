/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ARCH_POWERPC_LOCAL_H
#define _ARCH_POWERPC_LOCAL_H

#ifdef CONFIG_PPC_BOOK3S_64

#include <linux/percpu.h>
#include <linux/atomic.h>
#include <linux/irqflags.h>

#include <asm/hw_irq.h>

typedef struct
{
	long v;
} local_t;

#define LOCAL_INIT(i)	{ (i) }

static __inline__ long local_read(local_t *l)
{
	return READ_ONCE(l->v);
}

static __inline__ void local_set(local_t *l, long i)
{
	WRITE_ONCE(l->v, i);
}

#define LOCAL_OP(op, c_op)						\
static __inline__ void local_##op(long i, local_t *l)			\
{									\
	unsigned long flags;						\
									\
	powerpc_local_irq_pmu_save(flags);				\
	l->v c_op i;						\
	powerpc_local_irq_pmu_restore(flags);				\
}

#define LOCAL_OP_RETURN(op, c_op)					\
static __inline__ long local_##op##_return(long a, local_t *l)		\
{									\
	long t;								\
	unsigned long flags;						\
									\
	powerpc_local_irq_pmu_save(flags);				\
	t = (l->v c_op a);						\
	powerpc_local_irq_pmu_restore(flags);				\
									\
	return t;							\
}

#define LOCAL_OPS(op, c_op)		\
	LOCAL_OP(op, c_op)		\
	LOCAL_OP_RETURN(op, c_op)

LOCAL_OPS(add, +=)
LOCAL_OPS(sub, -=)

#define local_add_negative(a, l)	(local_add_return((a), (l)) < 0)
#define local_inc_return(l)		local_add_return(1LL, l)
#define local_inc(l)			local_inc_return(l)

/*
 * local_inc_and_test - increment and test
 * @l: pointer of type local_t
 *
 * Atomically increments @l by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
#define local_inc_and_test(l)		(local_inc_return(l) == 0)

#define local_dec_return(l)		local_sub_return(1LL, l)
#define local_dec(l)			local_dec_return(l)
#define local_sub_and_test(a, l)	(local_sub_return((a), (l)) == 0)
#define local_dec_and_test(l)		(local_dec_return((l)) == 0)

static __inline__ long local_cmpxchg(local_t *l, long o, long n)
{
	long t;
	unsigned long flags;

	powerpc_local_irq_pmu_save(flags);
	t = l->v;
	if (t == o)
		l->v = n;
	powerpc_local_irq_pmu_restore(flags);

	return t;
}

static __inline__ long local_xchg(local_t *l, long n)
{
	long t;
	unsigned long flags;

	powerpc_local_irq_pmu_save(flags);
	t = l->v;
	l->v = n;
	powerpc_local_irq_pmu_restore(flags);

	return t;
}

/**
 * local_add_unless - add unless the number is a given value
 * @l: pointer of type local_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @l, so long as it was not @u.
 * Returns non-zero if @l was not @u, and zero otherwise.
 */
static __inline__ int local_add_unless(local_t *l, long a, long u)
{
	unsigned long flags;
	int ret = 0;

	powerpc_local_irq_pmu_save(flags);
	if (l->v != u) {
		l->v += a;
		ret = 1;
	}
	powerpc_local_irq_pmu_restore(flags);

	return ret;
}

#define local_inc_not_zero(l)		local_add_unless((l), 1, 0)

/* Use these for per-cpu local_t variables: on some archs they are
 * much more efficient than these naive implementations.  Note they take
 * a variable, not an address.
 */

#define __local_inc(l)		((l)->v++)
#define __local_dec(l)		((l)->v++)
#define __local_add(i,l)	((l)->v+=(i))
#define __local_sub(i,l)	((l)->v-=(i))

#else /* CONFIG_PPC64 */

#include <asm-generic/local.h>

#endif /* CONFIG_PPC64 */

#endif /* _ARCH_POWERPC_LOCAL_H */
