#ifndef __ASM_SH_CMPXCHG_LLSC_H
#define __ASM_SH_CMPXCHG_LLSC_H

#include <linux/bitops.h>
#include <asm/byteorder.h>

static inline unsigned long xchg_u32(volatile u32 *m, unsigned long val)
{
	unsigned long retval;
	unsigned long tmp;

	__asm__ __volatile__ (
		"1:					\n\t"
		"movli.l	@%2, %0	! xchg_u32	\n\t"
		"mov		%0, %1			\n\t"
		"mov		%3, %0			\n\t"
		"movco.l	%0, @%2			\n\t"
		"bf		1b			\n\t"
		"synco					\n\t"
		: "=&z"(tmp), "=&r" (retval)
		: "r" (m), "r" (val)
		: "t", "memory"
	);

	return retval;
}

static inline unsigned long
__cmpxchg_u32(volatile u32 *m, unsigned long old, unsigned long new)
{
	unsigned long retval;
	unsigned long tmp;

	__asm__ __volatile__ (
		"1:						\n\t"
		"movli.l	@%2, %0	! __cmpxchg_u32		\n\t"
		"mov		%0, %1				\n\t"
		"cmp/eq		%1, %3				\n\t"
		"bf		2f				\n\t"
		"mov		%4, %0				\n\t"
		"2:						\n\t"
		"movco.l	%0, @%2				\n\t"
		"bf		1b				\n\t"
		"synco						\n\t"
		: "=&z" (tmp), "=&r" (retval)
		: "r" (m), "r" (old), "r" (new)
		: "t", "memory"
	);

	return retval;
}

static inline u32 __xchg_cmpxchg(volatile void *ptr, u32 x, int size)
{
	int off = (unsigned long)ptr % sizeof(u32);
	volatile u32 *p = ptr - off;
#ifdef __BIG_ENDIAN
	int bitoff = (sizeof(u32) - 1 - off) * BITS_PER_BYTE;
#else
	int bitoff = off * BITS_PER_BYTE;
#endif
	u32 bitmask = ((0x1 << size * BITS_PER_BYTE) - 1) << bitoff;
	u32 oldv, newv;
	u32 ret;

	do {
		oldv = READ_ONCE(*p);
		ret = (oldv & bitmask) >> bitoff;
		newv = (oldv & ~bitmask) | (x << bitoff);
	} while (__cmpxchg_u32(p, oldv, newv) != oldv);

	return ret;
}

static inline unsigned long xchg_u16(volatile u16 *m, unsigned long val)
{
	return __xchg_cmpxchg(m, val, sizeof *m);
}

static inline unsigned long xchg_u8(volatile u8 *m, unsigned long val)
{
	return __xchg_cmpxchg(m, val, sizeof *m);
}

#endif /* __ASM_SH_CMPXCHG_LLSC_H */
