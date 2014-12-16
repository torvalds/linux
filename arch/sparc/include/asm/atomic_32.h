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

#include <asm/cmpxchg.h>
#include <asm/barrier.h>
#include <asm-generic/atomic64.h>


#define ATOMIC_INIT(i)  { (i) }

int atomic_add_return(int, atomic_t *);
int atomic_cmpxchg(atomic_t *, int, int);
int atomic_xchg(atomic_t *, int);
int __atomic_add_unless(atomic_t *, int, int);
void atomic_set(atomic_t *, int);

#define atomic_read(v)          ACCESS_ONCE((v)->counter)

#define atomic_add(i, v)	((void)atomic_add_return( (int)(i), (v)))
#define atomic_sub(i, v)	((void)atomic_add_return(-(int)(i), (v)))
#define atomic_inc(v)		((void)atomic_add_return(        1, (v)))
#define atomic_dec(v)		((void)atomic_add_return(       -1, (v)))

#define atomic_sub_return(i, v)	(atomic_add_return(-(int)(i), (v)))
#define atomic_inc_return(v)	(atomic_add_return(        1, (v)))
#define atomic_dec_return(v)	(atomic_add_return(       -1, (v)))

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

#endif /* !(__ARCH_SPARC_ATOMIC__) */
