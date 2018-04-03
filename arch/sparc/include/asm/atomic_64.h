/* atomic.h: Thankfully the V9 is at least reasonable for this
 *           stuff.
 *
 * Copyright (C) 1996, 1997, 2000, 2012 David S. Miller (davem@redhat.com)
 */

#ifndef __ARCH_SPARC64_ATOMIC__
#define __ARCH_SPARC64_ATOMIC__

#include <linux/types.h>
#include <asm/cmpxchg.h>
#include <asm/barrier.h>

#define ATOMIC_INIT(i)		{ (i) }
#define ATOMIC64_INIT(i)	{ (i) }

#define atomic_read(v)		READ_ONCE((v)->counter)
#define atomic64_read(v)	READ_ONCE((v)->counter)

#define atomic_set(v, i)	WRITE_ONCE(((v)->counter), (i))
#define atomic64_set(v, i)	WRITE_ONCE(((v)->counter), (i))

#define ATOMIC_OP(op)							\
void atomic_##op(int, atomic_t *);					\
void atomic64_##op(long, atomic64_t *);

#define ATOMIC_OP_RETURN(op)						\
int atomic_##op##_return(int, atomic_t *);				\
long atomic64_##op##_return(long, atomic64_t *);

#define ATOMIC_OPS(op) ATOMIC_OP(op) ATOMIC_OP_RETURN(op)

ATOMIC_OPS(add)
ATOMIC_OPS(sub)

ATOMIC_OP(and)
ATOMIC_OP(or)
ATOMIC_OP(xor)

#undef ATOMIC_OPS
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

#define atomic_dec_return(v)   atomic_sub_return(1, v)
#define atomic64_dec_return(v) atomic64_sub_return(1, v)

#define atomic_inc_return(v)   atomic_add_return(1, v)
#define atomic64_inc_return(v) atomic64_add_return(1, v)

/*
 * atomic_inc_and_test - increment and test
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
#define atomic_inc_and_test(v) (atomic_inc_return(v) == 0)
#define atomic64_inc_and_test(v) (atomic64_inc_return(v) == 0)

#define atomic_sub_and_test(i, v) (atomic_sub_return(i, v) == 0)
#define atomic64_sub_and_test(i, v) (atomic64_sub_return(i, v) == 0)

#define atomic_dec_and_test(v) (atomic_sub_return(1, v) == 0)
#define atomic64_dec_and_test(v) (atomic64_sub_return(1, v) == 0)

#define atomic_inc(v) atomic_add(1, v)
#define atomic64_inc(v) atomic64_add(1, v)

#define atomic_dec(v) atomic_sub(1, v)
#define atomic64_dec(v) atomic64_sub(1, v)

#define atomic_add_negative(i, v) (atomic_add_return(i, v) < 0)
#define atomic64_add_negative(i, v) (atomic64_add_return(i, v) < 0)

#define atomic_cmpxchg(v, o, n) (cmpxchg(&((v)->counter), (o), (n)))

static inline int atomic_xchg(atomic_t *v, int new)
{
	return xchg(&v->counter, new);
}

static inline int __atomic_add_unless(atomic_t *v, int a, int u)
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

#define atomic64_cmpxchg(v, o, n) \
	((__typeof__((v)->counter))cmpxchg(&((v)->counter), (o), (n)))
#define atomic64_xchg(v, new) (xchg(&((v)->counter), new))

static inline long atomic64_add_unless(atomic64_t *v, long a, long u)
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

long atomic64_dec_if_positive(atomic64_t *v);

#endif /* !(__ARCH_SPARC64_ATOMIC__) */
