/* atomic.h: These still suck, but the I-cache hit rate is higher.
 *
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 * Copyright (C) 2000 Anton Blanchard (anton@linuxcare.com.au)
 * Copyright (C) 2007 Kyle McMartin (kyle@parisc-linux.org)
 *
 * Additions by Keith M Wesolowski (wesolows@foobazco.org) based
 * on asm-parisc/atomic.h Copyright (C) 2000 Philipp Rumpf <prumpf@tux.org>.
 */

#ifndef __ARCH_SPARC_ATOMIC__
#define __ARCH_SPARC_ATOMIC__

#include <linux/types.h>

#ifdef __KERNEL__

#define ATOMIC_INIT(i)  { (i) }

extern int __atomic_add_return(int, atomic_t *);
extern int atomic_cmpxchg(atomic_t *, int, int);
#define atomic_xchg(v, new) (xchg(&((v)->counter), new))
extern int atomic_add_unless(atomic_t *, int, int);
extern void atomic_set(atomic_t *, int);

#define atomic_read(v)          ((v)->counter)

#define atomic_add(i, v)	((void)__atomic_add_return( (int)(i), (v)))
#define atomic_sub(i, v)	((void)__atomic_add_return(-(int)(i), (v)))
#define atomic_inc(v)		((void)__atomic_add_return(        1, (v)))
#define atomic_dec(v)		((void)__atomic_add_return(       -1, (v)))

#define atomic_add_return(i, v)	(__atomic_add_return( (int)(i), (v)))
#define atomic_sub_return(i, v)	(__atomic_add_return(-(int)(i), (v)))
#define atomic_inc_return(v)	(__atomic_add_return(        1, (v)))
#define atomic_dec_return(v)	(__atomic_add_return(       -1, (v)))

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

#define atomic_dec_and_test(v) (atomic_dec_return(v) == 0)
#define atomic_sub_and_test(i, v) (atomic_sub_return(i, v) == 0)

#define atomic_inc_not_zero(v) atomic_add_unless((v), 1, 0)

/* This is the old 24-bit implementation.  It's still used internally
 * by some sparc-specific code, notably the semaphore implementation.
 */
typedef struct { volatile int counter; } atomic24_t;

#ifndef CONFIG_SMP

#define ATOMIC24_INIT(i)  { (i) }
#define atomic24_read(v)          ((v)->counter)
#define atomic24_set(v, i)        (((v)->counter) = i)

#else
/* We do the bulk of the actual work out of line in two common
 * routines in assembler, see arch/sparc/lib/atomic.S for the
 * "fun" details.
 *
 * For SMP the trick is you embed the spin lock byte within
 * the word, use the low byte so signedness is easily retained
 * via a quick arithmetic shift.  It looks like this:
 *
 *	----------------------------------------
 *	| signed 24-bit counter value |  lock  |  atomic_t
 *	----------------------------------------
 *	 31                          8 7      0
 */

#define ATOMIC24_INIT(i)	{ ((i) << 8) }

static inline int atomic24_read(const atomic24_t *v)
{
	int ret = v->counter;

	while(ret & 0xff)
		ret = v->counter;

	return ret >> 8;
}

#define atomic24_set(v, i)	(((v)->counter) = ((i) << 8))
#endif

static inline int __atomic24_add(int i, atomic24_t *v)
{
	register volatile int *ptr asm("g1");
	register int increment asm("g2");
	register int tmp1 asm("g3");
	register int tmp2 asm("g4");
	register int tmp3 asm("g7");

	ptr = &v->counter;
	increment = i;

	__asm__ __volatile__(
	"mov	%%o7, %%g4\n\t"
	"call	___atomic24_add\n\t"
	" add	%%o7, 8, %%o7\n"
	: "=&r" (increment), "=r" (tmp1), "=r" (tmp2), "=r" (tmp3)
	: "0" (increment), "r" (ptr)
	: "memory", "cc");

	return increment;
}

static inline int __atomic24_sub(int i, atomic24_t *v)
{
	register volatile int *ptr asm("g1");
	register int increment asm("g2");
	register int tmp1 asm("g3");
	register int tmp2 asm("g4");
	register int tmp3 asm("g7");

	ptr = &v->counter;
	increment = i;

	__asm__ __volatile__(
	"mov	%%o7, %%g4\n\t"
	"call	___atomic24_sub\n\t"
	" add	%%o7, 8, %%o7\n"
	: "=&r" (increment), "=r" (tmp1), "=r" (tmp2), "=r" (tmp3)
	: "0" (increment), "r" (ptr)
	: "memory", "cc");

	return increment;
}

#define atomic24_add(i, v) ((void)__atomic24_add((i), (v)))
#define atomic24_sub(i, v) ((void)__atomic24_sub((i), (v)))

#define atomic24_dec_return(v) __atomic24_sub(1, (v))
#define atomic24_inc_return(v) __atomic24_add(1, (v))

#define atomic24_sub_and_test(i, v) (__atomic24_sub((i), (v)) == 0)
#define atomic24_dec_and_test(v) (__atomic24_sub(1, (v)) == 0)

#define atomic24_inc(v) ((void)__atomic24_add(1, (v)))
#define atomic24_dec(v) ((void)__atomic24_sub(1, (v)))

#define atomic24_add_negative(i, v) (__atomic24_add((i), (v)) < 0)

/* Atomic operations are already serializing */
#define smp_mb__before_atomic_dec()	barrier()
#define smp_mb__after_atomic_dec()	barrier()
#define smp_mb__before_atomic_inc()	barrier()
#define smp_mb__after_atomic_inc()	barrier()

#endif /* !(__KERNEL__) */

#include <asm-generic/atomic.h>
#endif /* !(__ARCH_SPARC_ATOMIC__) */
