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
#define mb() do {  asm volatile("bcr 14,0" : : : "memory"); } while (0)
#else
#define mb() do {  asm volatile("bcr 15,0" : : : "memory"); } while (0)
#endif

#define rmb()				mb()
#define wmb()				mb()
#define read_barrier_depends()		do { } while(0)
#define smp_mb()			mb()
#define smp_rmb()			rmb()
#define smp_wmb()			wmb()
#define smp_read_barrier_depends()	read_barrier_depends()
#define smp_mb__before_clear_bit()	smp_mb()
#define smp_mb__after_clear_bit()	smp_mb()

#define set_mb(var, value)		do { var = value; mb(); } while (0)

#define smp_store_release(p, v)						\
do {									\
	compiletime_assert_atomic_type(*p);				\
	barrier();							\
	ACCESS_ONCE(*p) = (v);						\
} while (0)

#define smp_load_acquire(p)						\
({									\
	typeof(*p) ___p1 = ACCESS_ONCE(*p);				\
	compiletime_assert_atomic_type(*p);				\
	barrier();							\
	___p1;								\
})

#endif /* __ASM_BARRIER_H */
