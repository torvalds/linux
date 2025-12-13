#ifndef _M68K_BITOPS_H
#define _M68K_BITOPS_H
/*
 * Copyright 1992, Linus Torvalds.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <linux/compiler.h>
#include <asm/barrier.h>

/*
 *	Bit access functions vary across the ColdFire and 68k families.
 *	So we will break them out here, and then macro in the ones we want.
 *
 *	ColdFire - supports standard bset/bclr/bchg with register operand only
 *	68000    - supports standard bset/bclr/bchg with memory operand
 *	>= 68020 - also supports the bfset/bfclr/bfchg instructions
 *
 *	Although it is possible to use only the bset/bclr/bchg with register
 *	operands on all platforms you end up with larger generated code.
 *	So we use the best form possible on a given platform.
 */

static inline void bset_reg_set_bit(int nr, volatile unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;

	__asm__ __volatile__ ("bset %1,(%0)"
		:
		: "a" (p), "di" (nr & 7)
		: "memory");
}

static inline void bset_mem_set_bit(int nr, volatile unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;

	__asm__ __volatile__ ("bset %1,%0"
		: "+m" (*p)
		: "di" (nr & 7));
}

static inline void bfset_mem_set_bit(int nr, volatile unsigned long *vaddr)
{
	__asm__ __volatile__ ("bfset %1{%0:#1}"
		:
		: "d" (nr ^ 31), "o" (*vaddr)
		: "memory");
}

#if defined(CONFIG_COLDFIRE)
#define	set_bit(nr, vaddr)	bset_reg_set_bit(nr, vaddr)
#elif defined(CONFIG_CPU_HAS_NO_BITFIELDS)
#define	set_bit(nr, vaddr)	bset_mem_set_bit(nr, vaddr)
#else
#define set_bit(nr, vaddr)	(__builtin_constant_p(nr) ? \
				bset_mem_set_bit(nr, vaddr) : \
				bfset_mem_set_bit(nr, vaddr))
#endif

static __always_inline void
arch___set_bit(unsigned long nr, volatile unsigned long *addr)
{
	set_bit(nr, addr);
}

static inline void bclr_reg_clear_bit(int nr, volatile unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;

	__asm__ __volatile__ ("bclr %1,(%0)"
		:
		: "a" (p), "di" (nr & 7)
		: "memory");
}

static inline void bclr_mem_clear_bit(int nr, volatile unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;

	__asm__ __volatile__ ("bclr %1,%0"
		: "+m" (*p)
		: "di" (nr & 7));
}

static inline void bfclr_mem_clear_bit(int nr, volatile unsigned long *vaddr)
{
	__asm__ __volatile__ ("bfclr %1{%0:#1}"
		:
		: "d" (nr ^ 31), "o" (*vaddr)
		: "memory");
}

#if defined(CONFIG_COLDFIRE)
#define	clear_bit(nr, vaddr)	bclr_reg_clear_bit(nr, vaddr)
#elif defined(CONFIG_CPU_HAS_NO_BITFIELDS)
#define	clear_bit(nr, vaddr)	bclr_mem_clear_bit(nr, vaddr)
#else
#define clear_bit(nr, vaddr)	(__builtin_constant_p(nr) ? \
				bclr_mem_clear_bit(nr, vaddr) : \
				bfclr_mem_clear_bit(nr, vaddr))
#endif

static __always_inline void
arch___clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	clear_bit(nr, addr);
}

static inline void bchg_reg_change_bit(int nr, volatile unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;

	__asm__ __volatile__ ("bchg %1,(%0)"
		:
		: "a" (p), "di" (nr & 7)
		: "memory");
}

static inline void bchg_mem_change_bit(int nr, volatile unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;

	__asm__ __volatile__ ("bchg %1,%0"
		: "+m" (*p)
		: "di" (nr & 7));
}

static inline void bfchg_mem_change_bit(int nr, volatile unsigned long *vaddr)
{
	__asm__ __volatile__ ("bfchg %1{%0:#1}"
		:
		: "d" (nr ^ 31), "o" (*vaddr)
		: "memory");
}

#if defined(CONFIG_COLDFIRE)
#define	change_bit(nr, vaddr)	bchg_reg_change_bit(nr, vaddr)
#elif defined(CONFIG_CPU_HAS_NO_BITFIELDS)
#define	change_bit(nr, vaddr)	bchg_mem_change_bit(nr, vaddr)
#else
#define change_bit(nr, vaddr)	(__builtin_constant_p(nr) ? \
				bchg_mem_change_bit(nr, vaddr) : \
				bfchg_mem_change_bit(nr, vaddr))
#endif

static __always_inline void
arch___change_bit(unsigned long nr, volatile unsigned long *addr)
{
	change_bit(nr, addr);
}

#define arch_test_bit generic_test_bit
#define arch_test_bit_acquire generic_test_bit_acquire

static inline int bset_reg_test_and_set_bit(int nr,
					    volatile unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;
	char retval;

	__asm__ __volatile__ ("bset %2,(%1); sne %0"
		: "=d" (retval)
		: "a" (p), "di" (nr & 7)
		: "memory");
	return retval;
}

static inline int bset_mem_test_and_set_bit(int nr,
					    volatile unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;
	char retval;

	__asm__ __volatile__ ("bset %2,%1; sne %0"
		: "=d" (retval), "+m" (*p)
		: "di" (nr & 7));
	return retval;
}

static inline int bfset_mem_test_and_set_bit(int nr,
					     volatile unsigned long *vaddr)
{
	char retval;

	__asm__ __volatile__ ("bfset %2{%1:#1}; sne %0"
		: "=d" (retval)
		: "d" (nr ^ 31), "o" (*vaddr)
		: "memory");
	return retval;
}

#if defined(CONFIG_COLDFIRE)
#define	test_and_set_bit(nr, vaddr)	bset_reg_test_and_set_bit(nr, vaddr)
#elif defined(CONFIG_CPU_HAS_NO_BITFIELDS)
#define	test_and_set_bit(nr, vaddr)	bset_mem_test_and_set_bit(nr, vaddr)
#else
#define test_and_set_bit(nr, vaddr)	(__builtin_constant_p(nr) ? \
					bset_mem_test_and_set_bit(nr, vaddr) : \
					bfset_mem_test_and_set_bit(nr, vaddr))
#endif

static __always_inline bool
arch___test_and_set_bit(unsigned long nr, volatile unsigned long *addr)
{
	return test_and_set_bit(nr, addr);
}

static inline int bclr_reg_test_and_clear_bit(int nr,
					      volatile unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;
	char retval;

	__asm__ __volatile__ ("bclr %2,(%1); sne %0"
		: "=d" (retval)
		: "a" (p), "di" (nr & 7)
		: "memory");
	return retval;
}

static inline int bclr_mem_test_and_clear_bit(int nr,
					      volatile unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;
	char retval;

	__asm__ __volatile__ ("bclr %2,%1; sne %0"
		: "=d" (retval), "+m" (*p)
		: "di" (nr & 7));
	return retval;
}

static inline int bfclr_mem_test_and_clear_bit(int nr,
					       volatile unsigned long *vaddr)
{
	char retval;

	__asm__ __volatile__ ("bfclr %2{%1:#1}; sne %0"
		: "=d" (retval)
		: "d" (nr ^ 31), "o" (*vaddr)
		: "memory");
	return retval;
}

#if defined(CONFIG_COLDFIRE)
#define	test_and_clear_bit(nr, vaddr)	bclr_reg_test_and_clear_bit(nr, vaddr)
#elif defined(CONFIG_CPU_HAS_NO_BITFIELDS)
#define	test_and_clear_bit(nr, vaddr)	bclr_mem_test_and_clear_bit(nr, vaddr)
#else
#define test_and_clear_bit(nr, vaddr)	(__builtin_constant_p(nr) ? \
					bclr_mem_test_and_clear_bit(nr, vaddr) : \
					bfclr_mem_test_and_clear_bit(nr, vaddr))
#endif

static __always_inline bool
arch___test_and_clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	return test_and_clear_bit(nr, addr);
}

static inline int bchg_reg_test_and_change_bit(int nr,
					       volatile unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;
	char retval;

	__asm__ __volatile__ ("bchg %2,(%1); sne %0"
		: "=d" (retval)
		: "a" (p), "di" (nr & 7)
		: "memory");
	return retval;
}

static inline int bchg_mem_test_and_change_bit(int nr,
					       volatile unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;
	char retval;

	__asm__ __volatile__ ("bchg %2,%1; sne %0"
		: "=d" (retval), "+m" (*p)
		: "di" (nr & 7));
	return retval;
}

static inline int bfchg_mem_test_and_change_bit(int nr,
						volatile unsigned long *vaddr)
{
	char retval;

	__asm__ __volatile__ ("bfchg %2{%1:#1}; sne %0"
		: "=d" (retval)
		: "d" (nr ^ 31), "o" (*vaddr)
		: "memory");
	return retval;
}

#if defined(CONFIG_COLDFIRE)
#define	test_and_change_bit(nr, vaddr)	bchg_reg_test_and_change_bit(nr, vaddr)
#elif defined(CONFIG_CPU_HAS_NO_BITFIELDS)
#define	test_and_change_bit(nr, vaddr)	bchg_mem_test_and_change_bit(nr, vaddr)
#else
#define test_and_change_bit(nr, vaddr)	(__builtin_constant_p(nr) ? \
					bchg_mem_test_and_change_bit(nr, vaddr) : \
					bfchg_mem_test_and_change_bit(nr, vaddr))
#endif

static __always_inline bool
arch___test_and_change_bit(unsigned long nr, volatile unsigned long *addr)
{
	return test_and_change_bit(nr, addr);
}

static inline bool xor_unlock_is_negative_byte(unsigned long mask,
		volatile unsigned long *p)
{
#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__ ("eorl %1, %0"
		: "+m" (*p)
		: "d" (mask)
		: "memory");
	return *p & (1 << 7);
#else
	char result;
	char *cp = (char *)p + 3;	/* m68k is big-endian */

	__asm__ __volatile__ ("eor.b %1, %2; smi %0"
		: "=d" (result)
		: "di" (mask), "o" (*cp)
		: "memory");
	return result;
#endif
}

/*
 *	The true 68020 and more advanced processors support the "bfffo"
 *	instruction for finding bits. ColdFire and simple 68000 parts
 *	(including CPU32) do not support this. They simply use the generic
 *	functions.
 */
#if defined(CONFIG_CPU_HAS_NO_BITFIELDS)
#include <asm-generic/bitops/ffz.h>
#else

static inline unsigned long find_first_zero_bit(const unsigned long *vaddr,
						unsigned long size)
{
	const unsigned long *p = vaddr;
	unsigned long res = 32;
	unsigned long words;
	unsigned long num;

	if (!size)
		return 0;

	words = (size + 31) >> 5;
	while (!(num = ~*p++)) {
		if (!--words)
			goto out;
	}

	__asm__ __volatile__ ("bfffo %1{#0,#0},%0"
			      : "=d" (res) : "d" (num & -num));
	res ^= 31;
out:
	res += ((long)p - (long)vaddr - 4) * 8;
	return res < size ? res : size;
}
#define find_first_zero_bit find_first_zero_bit

static inline unsigned long find_next_zero_bit(const unsigned long *vaddr,
					       unsigned long size,
					       unsigned long offset)
{
	const unsigned long *p = vaddr + (offset >> 5);
	int bit = offset & 31UL, res;

	if (offset >= size)
		return size;

	if (bit) {
		unsigned long num = ~*p++ & (~0UL << bit);
		offset -= bit;

		/* Look for zero in first longword */
		__asm__ __volatile__ ("bfffo %1{#0,#0},%0"
				      : "=d" (res) : "d" (num & -num));
		if (res < 32) {
			offset += res ^ 31;
			return offset < size ? offset : size;
		}
		offset += 32;

		if (offset >= size)
			return size;
	}
	/* No zero yet, search remaining full bytes for a zero */
	return offset + find_first_zero_bit(p, size - offset);
}
#define find_next_zero_bit find_next_zero_bit

static inline unsigned long find_first_bit(const unsigned long *vaddr,
					   unsigned long size)
{
	const unsigned long *p = vaddr;
	unsigned long res = 32;
	unsigned long words;
	unsigned long num;

	if (!size)
		return 0;

	words = (size + 31) >> 5;
	while (!(num = *p++)) {
		if (!--words)
			goto out;
	}

	__asm__ __volatile__ ("bfffo %1{#0,#0},%0"
			      : "=d" (res) : "d" (num & -num));
	res ^= 31;
out:
	res += ((long)p - (long)vaddr - 4) * 8;
	return res < size ? res : size;
}
#define find_first_bit find_first_bit

static inline unsigned long find_next_bit(const unsigned long *vaddr,
					  unsigned long size,
					  unsigned long offset)
{
	const unsigned long *p = vaddr + (offset >> 5);
	int bit = offset & 31UL, res;

	if (offset >= size)
		return size;

	if (bit) {
		unsigned long num = *p++ & (~0UL << bit);
		offset -= bit;

		/* Look for one in first longword */
		__asm__ __volatile__ ("bfffo %1{#0,#0},%0"
				      : "=d" (res) : "d" (num & -num));
		if (res < 32) {
			offset += res ^ 31;
			return offset < size ? offset : size;
		}
		offset += 32;

		if (offset >= size)
			return size;
	}
	/* No one yet, search remaining full bytes for a one */
	return offset + find_first_bit(p, size - offset);
}
#define find_next_bit find_next_bit

/*
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 */
static inline unsigned long __attribute_const__ ffz(unsigned long word)
{
	int res;

	__asm__ __volatile__ ("bfffo %1{#0,#0},%0"
			      : "=d" (res) : "d" (~word & -~word));
	return res ^ 31;
}

#endif

#ifdef __KERNEL__

#if defined(CONFIG_CPU_HAS_NO_BITFIELDS)

/*
 *	The newer ColdFire family members support a "bitrev" instruction
 *	and we can use that to implement a fast ffs. Older Coldfire parts,
 *	and normal 68000 parts don't have anything special, so we use the
 *	generic functions for those.
 */
#if (defined(__mcfisaaplus__) || defined(__mcfisac__)) && \
	!defined(CONFIG_M68000)
static inline __attribute_const__ unsigned long __ffs(unsigned long x)
{
	__asm__ __volatile__ ("bitrev %0; ff1 %0"
		: "=d" (x)
		: "0" (x));
	return x;
}

static inline __attribute_const__ int ffs(int x)
{
	if (!x)
		return 0;
	return __ffs(x) + 1;
}

#else
#include <asm-generic/bitops/ffs.h>
#include <asm-generic/bitops/__ffs.h>
#endif

#include <asm-generic/bitops/fls.h>
#include <asm-generic/bitops/__fls.h>

#else

/*
 *	ffs: find first bit set. This is defined the same way as
 *	the libc and compiler builtin ffs routines, therefore
 *	differs in spirit from the above ffz (man ffs).
 */
static inline __attribute_const__ int ffs(int x)
{
	int cnt;

	__asm__ ("bfffo %1{#0:#0},%0"
		: "=d" (cnt)
		: "dm" (x & -x));
	return 32 - cnt;
}

static inline __attribute_const__ unsigned long __ffs(unsigned long x)
{
	return ffs(x) - 1;
}

/*
 *	fls: find last bit set.
 */
static inline __attribute_const__ int fls(unsigned int x)
{
	int cnt;

	__asm__ ("bfffo %1{#0,#0},%0"
		: "=d" (cnt)
		: "dm" (x));
	return 32 - cnt;
}

static inline __attribute_const__ unsigned long __fls(unsigned long x)
{
	return fls(x) - 1;
}

#endif

/* Simple test-and-set bit locks */
#define test_and_set_bit_lock	test_and_set_bit
#define clear_bit_unlock	clear_bit
#define __clear_bit_unlock	clear_bit_unlock

#include <asm-generic/bitops/non-instrumented-non-atomic.h>
#include <asm-generic/bitops/ext2-atomic.h>
#include <asm-generic/bitops/fls64.h>
#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/le.h>
#endif /* __KERNEL__ */

#endif /* _M68K_BITOPS_H */
