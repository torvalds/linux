/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_BARRIER_H
#define __ASM_BARRIER_H

#ifndef __ASSEMBLY__

/* The synchronize caches instruction executes as a nop on systems in
   which all memory references are performed in order. */
#define synchronize_caches() __asm__ __volatile__ ("sync" : : : "memory")

#if defined(CONFIG_SMP)
#define mb()		do { synchronize_caches(); } while (0)
#define rmb()		mb()
#define wmb()		mb()
#define dma_rmb()	mb()
#define dma_wmb()	mb()
#else
#define mb()		barrier()
#define rmb()		barrier()
#define wmb()		barrier()
#define dma_rmb()	barrier()
#define dma_wmb()	barrier()
#endif

#define __smp_mb()	mb()
#define __smp_rmb()	mb()
#define __smp_wmb()	mb()

#include <asm-generic/barrier.h>

#endif /* !__ASSEMBLY__ */
#endif /* __ASM_BARRIER_H */
