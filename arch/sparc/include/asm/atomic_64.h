/* SPDX-License-Identifier: GPL-2.0 */
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

#define ATOMIC64_INIT(i)	{ (i) }

#define arch_atomic_read(v)	READ_ONCE((v)->counter)
#define arch_atomic64_read(v)	READ_ONCE((v)->counter)

#define arch_atomic_set(v, i)	WRITE_ONCE(((v)->counter), (i))
#define arch_atomic64_set(v, i)	WRITE_ONCE(((v)->counter), (i))

#define ATOMIC_OP(op)							\
void arch_atomic_##op(int, atomic_t *);					\
void arch_atomic64_##op(s64, atomic64_t *);

#define ATOMIC_OP_RETURN(op)						\
int arch_atomic_##op##_return(int, atomic_t *);				\
s64 arch_atomic64_##op##_return(s64, atomic64_t *);

#define ATOMIC_FETCH_OP(op)						\
int arch_atomic_fetch_##op(int, atomic_t *);				\
s64 arch_atomic64_fetch_##op(s64, atomic64_t *);

#define ATOMIC_OPS(op) ATOMIC_OP(op) ATOMIC_OP_RETURN(op) ATOMIC_FETCH_OP(op)

ATOMIC_OPS(add)
ATOMIC_OPS(sub)

#undef ATOMIC_OPS
#define ATOMIC_OPS(op) ATOMIC_OP(op) ATOMIC_FETCH_OP(op)

ATOMIC_OPS(and)
ATOMIC_OPS(or)
ATOMIC_OPS(xor)

#undef ATOMIC_OPS
#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

#define arch_atomic_cmpxchg(v, o, n) (arch_cmpxchg(&((v)->counter), (o), (n)))

static inline int arch_atomic_xchg(atomic_t *v, int new)
{
	return arch_xchg(&v->counter, new);
}

#define arch_atomic64_cmpxchg(v, o, n) \
	((__typeof__((v)->counter))arch_cmpxchg(&((v)->counter), (o), (n)))
#define arch_atomic64_xchg(v, new) (arch_xchg(&((v)->counter), new))

s64 arch_atomic64_dec_if_positive(atomic64_t *v);
#define arch_atomic64_dec_if_positive arch_atomic64_dec_if_positive

#endif /* !(__ARCH_SPARC64_ATOMIC__) */
