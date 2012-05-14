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

static inline void mb(void)
{
	asm volatile("bcr 15,0" : : : "memory");
}

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

#endif /* __ASM_BARRIER_H */
