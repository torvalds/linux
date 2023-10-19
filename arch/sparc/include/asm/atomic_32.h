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

int arch_atomic_add_return(int, atomic_t *);
int arch_atomic_fetch_add(int, atomic_t *);
int arch_atomic_fetch_and(int, atomic_t *);
int arch_atomic_fetch_or(int, atomic_t *);
int arch_atomic_fetch_xor(int, atomic_t *);
int arch_atomic_cmpxchg(atomic_t *, int, int);
int arch_atomic_xchg(atomic_t *, int);
int arch_atomic_fetch_add_unless(atomic_t *, int, int);
void arch_atomic_set(atomic_t *, int);

#define arch_atomic_fetch_add_unless arch_atomic_fetch_add_unless

#define arch_atomic_set_release(v, i)	arch_atomic_set((v), (i))

#define arch_atomic_read(v)		READ_ONCE((v)->counter)

#define arch_atomic_add(i, v)	((void)arch_atomic_add_return( (int)(i), (v)))
#define arch_atomic_sub(i, v)	((void)arch_atomic_add_return(-(int)(i), (v)))

#define arch_atomic_and(i, v)	((void)arch_atomic_fetch_and((i), (v)))
#define arch_atomic_or(i, v)	((void)arch_atomic_fetch_or((i), (v)))
#define arch_atomic_xor(i, v)	((void)arch_atomic_fetch_xor((i), (v)))

#define arch_atomic_sub_return(i, v)	(arch_atomic_add_return(-(int)(i), (v)))
#define arch_atomic_fetch_sub(i, v)	(arch_atomic_fetch_add (-(int)(i), (v)))

#endif /* !(__ARCH_SPARC_ATOMIC__) */
