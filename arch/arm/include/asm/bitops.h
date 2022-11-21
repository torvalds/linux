/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 1995, Russell King.
 * Various bits and pieces copyrights include:
 *  Linus Torvalds (test_bit).
 * Big endian support: Copyright 2001, Nicolas Pitre
 *  reworked by rmk.
 *
 * bit 0 is the LSB of an "unsigned long" quantity.
 *
 * Please note that the code in this file should never be included
 * from user space.  Many of these are not implemented in assembler
 * since they would be too costly.  Also, they require privileged
 * instructions (which are not available from user mode) to ensure
 * that they are atomic.
 */

#ifndef __ASM_ARM_BITOPS_H
#define __ASM_ARM_BITOPS_H

#ifdef __KERNEL__

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <linux/compiler.h>
#include <linux/irqflags.h>
#include <asm/barrier.h>

/*
 * These functions are the basis of our bit ops.
 *
 * First, the atomic bitops. These use native endian.
 */
static inline void ____atomic_set_bit(unsigned int bit, volatile unsigned long *p)
{
	unsigned long flags;
	unsigned long mask = BIT_MASK(bit);

	p += BIT_WORD(bit);

	raw_local_irq_save(flags);
	*p |= mask;
	raw_local_irq_restore(flags);
}

static inline void ____atomic_clear_bit(unsigned int bit, volatile unsigned long *p)
{
	unsigned long flags;
	unsigned long mask = BIT_MASK(bit);

	p += BIT_WORD(bit);

	raw_local_irq_save(flags);
	*p &= ~mask;
	raw_local_irq_restore(flags);
}

static inline void ____atomic_change_bit(unsigned int bit, volatile unsigned long *p)
{
	unsigned long flags;
	unsigned long mask = BIT_MASK(bit);

	p += BIT_WORD(bit);

	raw_local_irq_save(flags);
	*p ^= mask;
	raw_local_irq_restore(flags);
}

static inline int
____atomic_test_and_set_bit(unsigned int bit, volatile unsigned long *p)
{
	unsigned long flags;
	unsigned int res;
	unsigned long mask = BIT_MASK(bit);

	p += BIT_WORD(bit);

	raw_local_irq_save(flags);
	res = *p;
	*p = res | mask;
	raw_local_irq_restore(flags);

	return (res & mask) != 0;
}

static inline int
____atomic_test_and_clear_bit(unsigned int bit, volatile unsigned long *p)
{
	unsigned long flags;
	unsigned int res;
	unsigned long mask = BIT_MASK(bit);

	p += BIT_WORD(bit);

	raw_local_irq_save(flags);
	res = *p;
	*p = res & ~mask;
	raw_local_irq_restore(flags);

	return (res & mask) != 0;
}

static inline int
____atomic_test_and_change_bit(unsigned int bit, volatile unsigned long *p)
{
	unsigned long flags;
	unsigned int res;
	unsigned long mask = BIT_MASK(bit);

	p += BIT_WORD(bit);

	raw_local_irq_save(flags);
	res = *p;
	*p = res ^ mask;
	raw_local_irq_restore(flags);

	return (res & mask) != 0;
}

#include <asm-generic/bitops/non-atomic.h>

/*
 *  A note about Endian-ness.
 *  -------------------------
 *
 * When the ARM is put into big endian mode via CR15, the processor
 * merely swaps the order of bytes within words, thus:
 *
 *          ------------ physical data bus bits -----------
 *          D31 ... D24  D23 ... D16  D15 ... D8  D7 ... D0
 * little     byte 3       byte 2       byte 1      byte 0
 * big        byte 0       byte 1       byte 2      byte 3
 *
 * This means that reading a 32-bit word at address 0 returns the same
 * value irrespective of the endian mode bit.
 *
 * Peripheral devices should be connected with the data bus reversed in
 * "Big Endian" mode.  ARM Application Note 61 is applicable, and is
 * available from http://www.arm.com/.
 *
 * The following assumes that the data bus connectivity for big endian
 * mode has been followed.
 *
 * Note that bit 0 is defined to be 32-bit word bit 0, not byte 0 bit 0.
 */

/*
 * Native endian assembly bitops.  nr = 0 -> word 0 bit 0.
 */
extern void _set_bit(int nr, volatile unsigned long * p);
extern void _clear_bit(int nr, volatile unsigned long * p);
extern void _change_bit(int nr, volatile unsigned long * p);
extern int _test_and_set_bit(int nr, volatile unsigned long * p);
extern int _test_and_clear_bit(int nr, volatile unsigned long * p);
extern int _test_and_change_bit(int nr, volatile unsigned long * p);

/*
 * Little endian assembly bitops.  nr = 0 -> byte 0 bit 0.
 */
extern int _find_first_zero_bit_le(const unsigned long *p, unsigned size);
extern int _find_next_zero_bit_le(const unsigned long *p, int size, int offset);
extern int _find_first_bit_le(const unsigned long *p, unsigned size);
extern int _find_next_bit_le(const unsigned long *p, int size, int offset);

/*
 * Big endian assembly bitops.  nr = 0 -> byte 3 bit 0.
 */
extern int _find_first_zero_bit_be(const unsigned long *p, unsigned size);
extern int _find_next_zero_bit_be(const unsigned long *p, int size, int offset);
extern int _find_first_bit_be(const unsigned long *p, unsigned size);
extern int _find_next_bit_be(const unsigned long *p, int size, int offset);

#ifndef CONFIG_SMP
/*
 * The __* form of bitops are non-atomic and may be reordered.
 */
#define ATOMIC_BITOP(name,nr,p)			\
	(__builtin_constant_p(nr) ? ____atomic_##name(nr, p) : _##name(nr,p))
#else
#define ATOMIC_BITOP(name,nr,p)		_##name(nr,p)
#endif

/*
 * Native endian atomic definitions.
 */
#define set_bit(nr,p)			ATOMIC_BITOP(set_bit,nr,p)
#define clear_bit(nr,p)			ATOMIC_BITOP(clear_bit,nr,p)
#define change_bit(nr,p)		ATOMIC_BITOP(change_bit,nr,p)
#define test_and_set_bit(nr,p)		ATOMIC_BITOP(test_and_set_bit,nr,p)
#define test_and_clear_bit(nr,p)	ATOMIC_BITOP(test_and_clear_bit,nr,p)
#define test_and_change_bit(nr,p)	ATOMIC_BITOP(test_and_change_bit,nr,p)

#ifndef __ARMEB__
/*
 * These are the little endian, atomic definitions.
 */
#define find_first_zero_bit(p,sz)	_find_first_zero_bit_le(p,sz)
#define find_next_zero_bit(p,sz,off)	_find_next_zero_bit_le(p,sz,off)
#define find_first_bit(p,sz)		_find_first_bit_le(p,sz)
#define find_next_bit(p,sz,off)		_find_next_bit_le(p,sz,off)

#else
/*
 * These are the big endian, atomic definitions.
 */
#define find_first_zero_bit(p,sz)	_find_first_zero_bit_be(p,sz)
#define find_next_zero_bit(p,sz,off)	_find_next_zero_bit_be(p,sz,off)
#define find_first_bit(p,sz)		_find_first_bit_be(p,sz)
#define find_next_bit(p,sz,off)		_find_next_bit_be(p,sz,off)

#endif

#if __LINUX_ARM_ARCH__ < 5

#include <asm-generic/bitops/__fls.h>
#include <asm-generic/bitops/__ffs.h>
#include <asm-generic/bitops/fls.h>
#include <asm-generic/bitops/ffs.h>

#else

/*
 * On ARMv5 and above, the gcc built-ins may rely on the clz instruction
 * and produce optimal inlined code in all cases. On ARMv7 it is even
 * better by also using the rbit instruction.
 */
#include <asm-generic/bitops/builtin-__fls.h>
#include <asm-generic/bitops/builtin-__ffs.h>
#include <asm-generic/bitops/builtin-fls.h>
#include <asm-generic/bitops/builtin-ffs.h>

#endif

#include <asm-generic/bitops/ffz.h>

#include <asm-generic/bitops/fls64.h>

#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/lock.h>

#ifdef __ARMEB__

static inline int find_first_zero_bit_le(const void *p, unsigned size)
{
	return _find_first_zero_bit_le(p, size);
}
#define find_first_zero_bit_le find_first_zero_bit_le

static inline int find_next_zero_bit_le(const void *p, int size, int offset)
{
	return _find_next_zero_bit_le(p, size, offset);
}
#define find_next_zero_bit_le find_next_zero_bit_le

static inline int find_next_bit_le(const void *p, int size, int offset)
{
	return _find_next_bit_le(p, size, offset);
}
#define find_next_bit_le find_next_bit_le

#endif

#include <asm-generic/bitops/le.h>

/*
 * Ext2 is defined to use little-endian byte ordering.
 */
#include <asm-generic/bitops/ext2-atomic-setbit.h>

#endif /* __KERNEL__ */

#endif /* _ARM_BITOPS_H */
