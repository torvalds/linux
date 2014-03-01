/*
 * Copyright 2004-2009 Analog Devices Inc.
 *               Tony Kou (tonyko@lineo.ca)
 *
 * Licensed under the GPL-2 or later
 */

#ifndef _BLACKFIN_BARRIER_H
#define _BLACKFIN_BARRIER_H

#include <asm/cache.h>

#define nop()  __asm__ __volatile__ ("nop;\n\t" : : )

/*
 * Force strict CPU ordering.
 */
#ifdef CONFIG_SMP

#ifdef __ARCH_SYNC_CORE_DCACHE
/* Force Core data cache coherence */
# define mb()	do { barrier(); smp_check_barrier(); smp_mark_barrier(); } while (0)
# define rmb()	do { barrier(); smp_check_barrier(); } while (0)
# define wmb()	do { barrier(); smp_mark_barrier(); } while (0)
# define read_barrier_depends()	do { barrier(); smp_check_barrier(); } while (0)
#endif

#endif /* !CONFIG_SMP */

#include <asm-generic/barrier.h>

#endif /* _BLACKFIN_BARRIER_H */
