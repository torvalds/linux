/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARCH_H8300_ATOMIC__
#define __ARCH_H8300_ATOMIC__

#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/cmpxchg.h>
#include <asm/irqflags.h>

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 */

#define atomic_read(v)		READ_ONCE((v)->counter)
#define atomic_set(v, i)	WRITE_ONCE(((v)->counter), (i))

#define ATOMIC_OP_RETURN(op, c_op)				\
static inline int atomic_##op##_return(int i, atomic_t *v)	\
{								\
	h8300flags flags;					\
	int ret;						\
								\
	flags = arch_local_irq_save();				\
	ret = v->counter c_op i;				\
	arch_local_irq_restore(flags);				\
	return ret;						\
}

#define ATOMIC_FETCH_OP(op, c_op)				\
static inline int atomic_fetch_##op(int i, atomic_t *v)		\
{								\
	h8300flags flags;					\
	int ret;						\
								\
	flags = arch_local_irq_save();				\
	ret = v->counter;					\
	v->counter c_op i;					\
	arch_local_irq_restore(flags);				\
	return ret;						\
}

#define ATOMIC_OP(op, c_op)					\
static inline void atomic_##op(int i, atomic_t *v)		\
{								\
	h8300flags flags;					\
								\
	flags = arch_local_irq_save();				\
	v->counter c_op i;					\
	arch_local_irq_restore(flags);				\
}

ATOMIC_OP_RETURN(add, +=)
ATOMIC_OP_RETURN(sub, -=)

#define ATOMIC_OPS(op, c_op)					\
	ATOMIC_OP(op, c_op)					\
	ATOMIC_FETCH_OP(op, c_op)

ATOMIC_OPS(and, &=)
ATOMIC_OPS(or,  |=)
ATOMIC_OPS(xor, ^=)
ATOMIC_OPS(add, +=)
ATOMIC_OPS(sub, -=)

#undef ATOMIC_OPS
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

static inline int atomic_cmpxchg(atomic_t *v, int old, int new)
{
	int ret;
	h8300flags flags;

	flags = arch_local_irq_save();
	ret = v->counter;
	if (likely(ret == old))
		v->counter = new;
	arch_local_irq_restore(flags);
	return ret;
}

static inline int atomic_fetch_add_unless(atomic_t *v, int a, int u)
{
	int ret;
	h8300flags flags;

	flags = arch_local_irq_save();
	ret = v->counter;
	if (ret != u)
		v->counter += a;
	arch_local_irq_restore(flags);
	return ret;
}
#define atomic_fetch_add_unless		atomic_fetch_add_unless

#endif /* __ARCH_H8300_ATOMIC __ */
