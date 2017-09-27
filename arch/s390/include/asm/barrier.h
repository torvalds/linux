/*
 * Copyright IBM Corp. 1999, 2009
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef __ASM_BARRIER_H
#define __ASM_BARRIER_H

/*
 * Force strict CPU ordering.
 * And yes, this is required on UP too when we're talking
 * to devices.
 */

#ifdef CONFIG_HAVE_MARCH_Z196_FEATURES
/* Fast-BCR without checkpoint synchronization */
#define __ASM_BARRIER "bcr 14,0\n"
#else
#define __ASM_BARRIER "bcr 15,0\n"
#endif

#define mb() do {  asm volatile(__ASM_BARRIER : : : "memory"); } while (0)

#define rmb()				barrier()
#define wmb()				barrier()
#define dma_rmb()			mb()
#define dma_wmb()			mb()
#define __smp_mb()			mb()
#define __smp_rmb()			rmb()
#define __smp_wmb()			wmb()

#define __smp_store_release(p, v)					\
do {									\
	compiletime_assert_atomic_type(*p);				\
	barrier();							\
	WRITE_ONCE(*p, v);						\
} while (0)

#define __smp_load_acquire(p)						\
({									\
	typeof(*p) ___p1 = READ_ONCE(*p);				\
	compiletime_assert_atomic_type(*p);				\
	barrier();							\
	___p1;								\
})

#define __smp_mb__before_atomic()	barrier()
#define __smp_mb__after_atomic()	barrier()

#include <asm-generic/barrier.h>

#endif /* __ASM_BARRIER_H */
