#ifndef _BLACKFIN_BITOPS_H
#define _BLACKFIN_BITOPS_H

/*
 * Copyright 1992, Linus Torvalds.
 */

#include <linux/compiler.h>
#include <asm/byteorder.h>	/* swab32 */

#ifdef __KERNEL__

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <asm-generic/bitops/ffs.h>
#include <asm-generic/bitops/__ffs.h>
#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/ffz.h>

#ifdef CONFIG_SMP

#include <linux/linkage.h>

asmlinkage int __raw_bit_set_asm(volatile unsigned long *addr, int nr);

asmlinkage int __raw_bit_clear_asm(volatile unsigned long *addr, int nr);

asmlinkage int __raw_bit_toggle_asm(volatile unsigned long *addr, int nr);

asmlinkage int __raw_bit_test_set_asm(volatile unsigned long *addr, int nr);

asmlinkage int __raw_bit_test_clear_asm(volatile unsigned long *addr, int nr);

asmlinkage int __raw_bit_test_toggle_asm(volatile unsigned long *addr, int nr);

asmlinkage int __raw_bit_test_asm(const volatile unsigned long *addr, int nr);

static inline void set_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *a = addr + (nr >> 5);
	__raw_bit_set_asm(a, nr & 0x1f);
}

static inline void clear_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *a = addr + (nr >> 5);
	__raw_bit_clear_asm(a, nr & 0x1f);
}

static inline void change_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *a = addr + (nr >> 5);
	__raw_bit_toggle_asm(a, nr & 0x1f);
}

static inline int test_bit(int nr, const volatile unsigned long *addr)
{
	volatile const unsigned long *a = addr + (nr >> 5);
	return __raw_bit_test_asm(a, nr & 0x1f) != 0;
}

static inline int test_and_set_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *a = addr + (nr >> 5);
	return __raw_bit_test_set_asm(a, nr & 0x1f);
}

static inline int test_and_clear_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *a = addr + (nr >> 5);
	return __raw_bit_test_clear_asm(a, nr & 0x1f);
}

static inline int test_and_change_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *a = addr + (nr >> 5);
	return __raw_bit_test_toggle_asm(a, nr & 0x1f);
}

#else /* !CONFIG_SMP */

#include <asm/system.h>		/* save_flags */

static inline void set_bit(int nr, volatile unsigned long *addr)
{
	int *a = (int *)addr;
	int mask;
	unsigned long flags;
	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	local_irq_save_hw(flags);
	*a |= mask;
	local_irq_restore_hw(flags);
}

static inline void clear_bit(int nr, volatile unsigned long *addr)
{
	int *a = (int *)addr;
	int mask;
	unsigned long flags;
	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	local_irq_save_hw(flags);
	*a &= ~mask;
	local_irq_restore_hw(flags);
}

static inline void change_bit(int nr, volatile unsigned long *addr)
{
	int mask;
	unsigned long flags;
	unsigned long *ADDR = (unsigned long *)addr;

	ADDR += nr >> 5;
	mask = 1 << (nr & 31);
	local_irq_save_hw(flags);
	*ADDR ^= mask;
	local_irq_restore_hw(flags);
}

static inline int test_and_set_bit(int nr, volatile unsigned long *addr)
{
	int mask, retval;
	volatile unsigned int *a = (volatile unsigned int *)addr;
	unsigned long flags;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	local_irq_save_hw(flags);
	retval = (mask & *a) != 0;
	*a |= mask;
	local_irq_restore_hw(flags);

	return retval;
}

static inline int test_and_clear_bit(int nr, volatile unsigned long *addr)
{
	int mask, retval;
	volatile unsigned int *a = (volatile unsigned int *)addr;
	unsigned long flags;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	local_irq_save_hw(flags);
	retval = (mask & *a) != 0;
	*a &= ~mask;
	local_irq_restore_hw(flags);

	return retval;
}

static inline int test_and_change_bit(int nr, volatile unsigned long *addr)
{
	int mask, retval;
	volatile unsigned int *a = (volatile unsigned int *)addr;
	unsigned long flags;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	local_irq_save_hw(flags);
	retval = (mask & *a) != 0;
	*a ^= mask;
	local_irq_restore_hw(flags);
	return retval;
}

#endif /* CONFIG_SMP */

/*
 * clear_bit() doesn't provide any barrier for the compiler.
 */
#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()

static inline void __set_bit(int nr, volatile unsigned long *addr)
{
	int *a = (int *)addr;
	int mask;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	*a |= mask;
}

static inline void __clear_bit(int nr, volatile unsigned long *addr)
{
	int *a = (int *)addr;
	int mask;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	*a &= ~mask;
}

static inline void __change_bit(int nr, volatile unsigned long *addr)
{
	int mask;
	unsigned long *ADDR = (unsigned long *)addr;

	ADDR += nr >> 5;
	mask = 1 << (nr & 31);
	*ADDR ^= mask;
}

static inline int __test_and_set_bit(int nr, volatile unsigned long *addr)
{
	int mask, retval;
	volatile unsigned int *a = (volatile unsigned int *)addr;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	retval = (mask & *a) != 0;
	*a |= mask;
	return retval;
}

static inline int __test_and_clear_bit(int nr, volatile unsigned long *addr)
{
	int mask, retval;
	volatile unsigned int *a = (volatile unsigned int *)addr;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	retval = (mask & *a) != 0;
	*a &= ~mask;
	return retval;
}

static inline int __test_and_change_bit(int nr,
					    volatile unsigned long *addr)
{
	int mask, retval;
	volatile unsigned int *a = (volatile unsigned int *)addr;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	retval = (mask & *a) != 0;
	*a ^= mask;
	return retval;
}

static inline int __test_bit(int nr, const void *addr)
{
	int *a = (int *)addr;
	int mask;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	return ((mask & *a) != 0);
}

#ifndef CONFIG_SMP
/*
 * This routine doesn't need irq save and restore ops in UP
 * context.
 */
static inline int test_bit(int nr, const void *addr)
{
	return __test_bit(nr, addr);
}
#endif

#include <asm-generic/bitops/find.h>
#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/lock.h>

#include <asm-generic/bitops/ext2-atomic.h>
#include <asm-generic/bitops/ext2-non-atomic.h>

#include <asm-generic/bitops/minix.h>

#endif				/* __KERNEL__ */

#include <asm-generic/bitops/fls.h>
#include <asm-generic/bitops/__fls.h>
#include <asm-generic/bitops/fls64.h>

#endif				/* _BLACKFIN_BITOPS_H */
