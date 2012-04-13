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
#else
# define mb()	barrier()
# define rmb()	barrier()
# define wmb()	barrier()
# define read_barrier_depends()	do { } while (0)
#endif

#else /* !CONFIG_SMP */

#define mb()	barrier()
#define rmb()	barrier()
#define wmb()	barrier()
#define read_barrier_depends()	do { } while (0)

#endif /* !CONFIG_SMP */

#define smp_mb()  mb()
#define smp_rmb() rmb()
#define smp_wmb() wmb()
#define set_mb(var, value) do { var = value; mb(); } while (0)
#define smp_read_barrier_depends()	read_barrier_depends()

#endif /* _BLACKFIN_BARRIER_H */
