#ifndef __ASM_METAG_BITOPS_H
#define __ASM_METAG_BITOPS_H

#include <linux/compiler.h>
#include <asm/barrier.h>
#include <asm/global_lock.h>

/*
 * clear_bit() doesn't provide any barrier for the compiler.
 */
#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()

#ifdef CONFIG_SMP
/*
 * These functions are the basis of our bit ops.
 */
static inline void set_bit(unsigned int bit, volatile unsigned long *p)
{
	unsigned long flags;
	unsigned long mask = 1UL << (bit & 31);

	p += bit >> 5;

	__global_lock1(flags);
	fence();
	*p |= mask;
	__global_unlock1(flags);
}

static inline void clear_bit(unsigned int bit, volatile unsigned long *p)
{
	unsigned long flags;
	unsigned long mask = 1UL << (bit & 31);

	p += bit >> 5;

	__global_lock1(flags);
	fence();
	*p &= ~mask;
	__global_unlock1(flags);
}

static inline void change_bit(unsigned int bit, volatile unsigned long *p)
{
	unsigned long flags;
	unsigned long mask = 1UL << (bit & 31);

	p += bit >> 5;

	__global_lock1(flags);
	fence();
	*p ^= mask;
	__global_unlock1(flags);
}

static inline int test_and_set_bit(unsigned int bit, volatile unsigned long *p)
{
	unsigned long flags;
	unsigned long old;
	unsigned long mask = 1UL << (bit & 31);

	p += bit >> 5;

	__global_lock1(flags);
	old = *p;
	if (!(old & mask)) {
		fence();
		*p = old | mask;
	}
	__global_unlock1(flags);

	return (old & mask) != 0;
}

static inline int test_and_clear_bit(unsigned int bit,
				     volatile unsigned long *p)
{
	unsigned long flags;
	unsigned long old;
	unsigned long mask = 1UL << (bit & 31);

	p += bit >> 5;

	__global_lock1(flags);
	old = *p;
	if (old & mask) {
		fence();
		*p = old & ~mask;
	}
	__global_unlock1(flags);

	return (old & mask) != 0;
}

static inline int test_and_change_bit(unsigned int bit,
				      volatile unsigned long *p)
{
	unsigned long flags;
	unsigned long old;
	unsigned long mask = 1UL << (bit & 31);

	p += bit >> 5;

	__global_lock1(flags);
	fence();
	old = *p;
	*p = old ^ mask;
	__global_unlock1(flags);

	return (old & mask) != 0;
}

#else
#include <asm-generic/bitops/atomic.h>
#endif /* CONFIG_SMP */

#include <asm-generic/bitops/non-atomic.h>
#include <asm-generic/bitops/find.h>
#include <asm-generic/bitops/ffs.h>
#include <asm-generic/bitops/__ffs.h>
#include <asm-generic/bitops/ffz.h>
#include <asm-generic/bitops/fls.h>
#include <asm-generic/bitops/__fls.h>
#include <asm-generic/bitops/fls64.h>
#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/lock.h>
#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/le.h>
#include <asm-generic/bitops/ext2-atomic.h>

#endif /* __ASM_METAG_BITOPS_H */
