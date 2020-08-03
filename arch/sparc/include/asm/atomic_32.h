/* SPDX-License-Identifier: GPL-2.0 */
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

int atomic_add_return(int, atomic_t *);
int atomic_fetch_add(int, atomic_t *);
int atomic_fetch_and(int, atomic_t *);
int atomic_fetch_or(int, atomic_t *);
int atomic_fetch_xor(int, atomic_t *);
int atomic_cmpxchg(atomic_t *, int, int);
int atomic_xchg(atomic_t *, int);
int atomic_fetch_add_unless(atomic_t *, int, int);
void atomic_set(atomic_t *, int);

#define atomic_fetch_add_unless	atomic_fetch_add_unless

#define atomic_set_release(v, i)	atomic_set((v), (i))

#define atomic_read(v)          READ_ONCE((v)->counter)

#define atomic_add(i, v)	((void)atomic_add_return( (int)(i), (v)))
#define atomic_sub(i, v)	((void)atomic_add_return(-(int)(i), (v)))

#define atomic_and(i, v)	((void)atomic_fetch_and((i), (v)))
#define atomic_or(i, v)		((void)atomic_fetch_or((i), (v)))
#define atomic_xor(i, v)	((void)atomic_fetch_xor((i), (v)))

#define atomic_sub_return(i, v)	(atomic_add_return(-(int)(i), (v)))
#define atomic_fetch_sub(i, v)  (atomic_fetch_add (-(int)(i), (v)))

#endif /* !(__ARCH_SPARC_ATOMIC__) */
