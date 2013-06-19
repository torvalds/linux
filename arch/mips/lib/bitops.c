/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1994-1997, 99, 2000, 06, 07 Ralf Baechle (ralf@linux-mips.org)
 * Copyright (c) 1999, 2000  Silicon Graphics, Inc.
 */
#include <linux/bitops.h>
#include <linux/irqflags.h>
#include <linux/export.h>


/**
 * __mips_set_bit - Atomically set a bit in memory.  This is called by
 * set_bit() if it cannot find a faster solution.
 * @nr: the bit to set
 * @addr: the address to start counting from
 */
void __mips_set_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long *a = (unsigned long *)addr;
	unsigned bit = nr & SZLONG_MASK;
	unsigned long mask;
	unsigned long flags;

	a += nr >> SZLONG_LOG;
	mask = 1UL << bit;
	raw_local_irq_save(flags);
	*a |= mask;
	raw_local_irq_restore(flags);
}
EXPORT_SYMBOL(__mips_set_bit);


/**
 * __mips_clear_bit - Clears a bit in memory.  This is called by clear_bit() if
 * it cannot find a faster solution.
 * @nr: Bit to clear
 * @addr: Address to start counting from
 */
void __mips_clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long *a = (unsigned long *)addr;
	unsigned bit = nr & SZLONG_MASK;
	unsigned long mask;
	unsigned long flags;

	a += nr >> SZLONG_LOG;
	mask = 1UL << bit;
	raw_local_irq_save(flags);
	*a &= ~mask;
	raw_local_irq_restore(flags);
}
EXPORT_SYMBOL(__mips_clear_bit);


/**
 * __mips_change_bit - Toggle a bit in memory.	This is called by change_bit()
 * if it cannot find a faster solution.
 * @nr: Bit to change
 * @addr: Address to start counting from
 */
void __mips_change_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long *a = (unsigned long *)addr;
	unsigned bit = nr & SZLONG_MASK;
	unsigned long mask;
	unsigned long flags;

	a += nr >> SZLONG_LOG;
	mask = 1UL << bit;
	raw_local_irq_save(flags);
	*a ^= mask;
	raw_local_irq_restore(flags);
}
EXPORT_SYMBOL(__mips_change_bit);


/**
 * __mips_test_and_set_bit - Set a bit and return its old value.  This is
 * called by test_and_set_bit() if it cannot find a faster solution.
 * @nr: Bit to set
 * @addr: Address to count from
 */
int __mips_test_and_set_bit(unsigned long nr,
			    volatile unsigned long *addr)
{
	unsigned long *a = (unsigned long *)addr;
	unsigned bit = nr & SZLONG_MASK;
	unsigned long mask;
	unsigned long flags;
	int res;

	a += nr >> SZLONG_LOG;
	mask = 1UL << bit;
	raw_local_irq_save(flags);
	res = (mask & *a) != 0;
	*a |= mask;
	raw_local_irq_restore(flags);
	return res;
}
EXPORT_SYMBOL(__mips_test_and_set_bit);


/**
 * __mips_test_and_set_bit_lock - Set a bit and return its old value.  This is
 * called by test_and_set_bit_lock() if it cannot find a faster solution.
 * @nr: Bit to set
 * @addr: Address to count from
 */
int __mips_test_and_set_bit_lock(unsigned long nr,
				 volatile unsigned long *addr)
{
	unsigned long *a = (unsigned long *)addr;
	unsigned bit = nr & SZLONG_MASK;
	unsigned long mask;
	unsigned long flags;
	int res;

	a += nr >> SZLONG_LOG;
	mask = 1UL << bit;
	raw_local_irq_save(flags);
	res = (mask & *a) != 0;
	*a |= mask;
	raw_local_irq_restore(flags);
	return res;
}
EXPORT_SYMBOL(__mips_test_and_set_bit_lock);


/**
 * __mips_test_and_clear_bit - Clear a bit and return its old value.  This is
 * called by test_and_clear_bit() if it cannot find a faster solution.
 * @nr: Bit to clear
 * @addr: Address to count from
 */
int __mips_test_and_clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long *a = (unsigned long *)addr;
	unsigned bit = nr & SZLONG_MASK;
	unsigned long mask;
	unsigned long flags;
	int res;

	a += nr >> SZLONG_LOG;
	mask = 1UL << bit;
	raw_local_irq_save(flags);
	res = (mask & *a) != 0;
	*a &= ~mask;
	raw_local_irq_restore(flags);
	return res;
}
EXPORT_SYMBOL(__mips_test_and_clear_bit);


/**
 * __mips_test_and_change_bit - Change a bit and return its old value.	This is
 * called by test_and_change_bit() if it cannot find a faster solution.
 * @nr: Bit to change
 * @addr: Address to count from
 */
int __mips_test_and_change_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long *a = (unsigned long *)addr;
	unsigned bit = nr & SZLONG_MASK;
	unsigned long mask;
	unsigned long flags;
	int res;

	a += nr >> SZLONG_LOG;
	mask = 1UL << bit;
	raw_local_irq_save(flags);
	res = (mask & *a) != 0;
	*a ^= mask;
	raw_local_irq_restore(flags);
	return res;
}
EXPORT_SYMBOL(__mips_test_and_change_bit);
