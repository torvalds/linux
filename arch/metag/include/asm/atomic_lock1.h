#ifndef __ASM_METAG_ATOMIC_LOCK1_H
#define __ASM_METAG_ATOMIC_LOCK1_H

#define ATOMIC_INIT(i)	{ (i) }

#include <linux/compiler.h>

#include <asm/barrier.h>
#include <asm/global_lock.h>

static inline int atomic_read(const atomic_t *v)
{
	return (v)->counter;
}

/*
 * atomic_set needs to be take the lock to protect atomic_add_unless from a
 * possible race, as it reads the counter twice:
 *
 *  CPU0                               CPU1
 *  atomic_add_unless(1, 0)
 *    ret = v->counter (non-zero)
 *    if (ret != u)                    v->counter = 0
 *      v->counter += 1 (counter set to 1)
 *
 * Making atomic_set take the lock ensures that ordering and logical
 * consistency is preserved.
 */
static inline int atomic_set(atomic_t *v, int i)
{
	unsigned long flags;

	__global_lock1(flags);
	fence();
	v->counter = i;
	__global_unlock1(flags);
	return i;
}

#define ATOMIC_OP(op, c_op)						\
static inline void atomic_##op(int i, atomic_t *v)			\
{									\
	unsigned long flags;						\
									\
	__global_lock1(flags);						\
	fence();							\
	v->counter c_op i;						\
	__global_unlock1(flags);					\
}									\

#define ATOMIC_OP_RETURN(op, c_op)					\
static inline int atomic_##op##_return(int i, atomic_t *v)		\
{									\
	unsigned long result;						\
	unsigned long flags;						\
									\
	__global_lock1(flags);						\
	result = v->counter;						\
	result c_op i;							\
	fence();							\
	v->counter = result;						\
	__global_unlock1(flags);					\
									\
	return result;							\
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

static inline int atomic_cmpxchg(atomic_t *v, int old, int new)
{
	int ret;
	unsigned long flags;

	__global_lock1(flags);
	ret = v->counter;
	if (ret == old) {
		fence();
		v->counter = new;
	}
	__global_unlock1(flags);

	return ret;
}

#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

static inline int __atomic_add_unless(atomic_t *v, int a, int u)
{
	int ret;
	unsigned long flags;

	__global_lock1(flags);
	ret = v->counter;
	if (ret != u) {
		fence();
		v->counter += a;
	}
	__global_unlock1(flags);

	return ret;
}

static inline int atomic_sub_if_positive(int i, atomic_t *v)
{
	int ret;
	unsigned long flags;

	__global_lock1(flags);
	ret = v->counter - 1;
	if (ret >= 0) {
		fence();
		v->counter = ret;
	}
	__global_unlock1(flags);

	return ret;
}

#endif /* __ASM_METAG_ATOMIC_LOCK1_H */
