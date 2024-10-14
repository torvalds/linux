/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 1999, 2009
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef __ASM_BARRIER_H
#define __ASM_BARRIER_H

#include <asm/march.h>

/*
 * Force strict CPU ordering.
 * And yes, this is required on UP too when we're talking
 * to devices.
 */

#ifdef MARCH_HAS_Z196_FEATURES
/* Fast-BCR without checkpoint synchronization */
#define __ASM_BCR_SERIALIZE "bcr 14,0\n"
#else
#define __ASM_BCR_SERIALIZE "bcr 15,0\n"
#endif

static __always_inline void bcr_serialize(void)
{
	asm volatile(__ASM_BCR_SERIALIZE : : : "memory");
}

#define __mb()		bcr_serialize()
#define __rmb()		barrier()
#define __wmb()		barrier()
#define __dma_rmb()	__mb()
#define __dma_wmb()	__mb()
#define __smp_mb()	__mb()
#define __smp_rmb()	__rmb()
#define __smp_wmb()	__wmb()

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

/**
 * array_index_mask_nospec - generate a mask for array_idx() that is
 * ~0UL when the bounds check succeeds and 0 otherwise
 * @index: array element index
 * @size: number of elements in array
 */
#define array_index_mask_nospec array_index_mask_nospec
static inline unsigned long array_index_mask_nospec(unsigned long index,
						    unsigned long size)
{
	unsigned long mask;

	if (__builtin_constant_p(size) && size > 0) {
		asm("	clgr	%2,%1\n"
		    "	slbgr	%0,%0\n"
		    :"=d" (mask) : "d" (size-1), "d" (index) :"cc");
		return mask;
	}
	asm("	clgr	%1,%2\n"
	    "	slbgr	%0,%0\n"
	    :"=d" (mask) : "d" (size), "d" (index) :"cc");
	return ~mask;
}

#include <asm-generic/barrier.h>

#endif /* __ASM_BARRIER_H */
