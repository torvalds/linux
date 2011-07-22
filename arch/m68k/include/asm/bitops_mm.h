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

/*
 * Require 68020 or better.
 *
 * They use the standard big-endian m680x0 bit ordering.
 */

#define test_and_set_bit(nr,vaddr) \
  (__builtin_constant_p(nr) ? \
   __constant_test_and_set_bit(nr, vaddr) : \
   __generic_test_and_set_bit(nr, vaddr))

#define __test_and_set_bit(nr,vaddr) test_and_set_bit(nr,vaddr)

static inline int __constant_test_and_set_bit(int nr, unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;
	char retval;

	__asm__ __volatile__ ("bset %2,%1; sne %0"
			: "=d" (retval), "+m" (*p)
			: "di" (nr & 7));

	return retval;
}

static inline int __generic_test_and_set_bit(int nr, unsigned long *vaddr)
{
	char retval;

	__asm__ __volatile__ ("bfset %2{%1:#1}; sne %0"
			: "=d" (retval) : "d" (nr^31), "o" (*vaddr) : "memory");

	return retval;
}

#define set_bit(nr,vaddr) \
  (__builtin_constant_p(nr) ? \
   __constant_set_bit(nr, vaddr) : \
   __generic_set_bit(nr, vaddr))

#define __set_bit(nr,vaddr) set_bit(nr,vaddr)

static inline void __constant_set_bit(int nr, volatile unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;
	__asm__ __volatile__ ("bset %1,%0"
			: "+m" (*p) : "di" (nr & 7));
}

static inline void __generic_set_bit(int nr, volatile unsigned long *vaddr)
{
	__asm__ __volatile__ ("bfset %1{%0:#1}"
			: : "d" (nr^31), "o" (*vaddr) : "memory");
}

#define test_and_clear_bit(nr,vaddr) \
  (__builtin_constant_p(nr) ? \
   __constant_test_and_clear_bit(nr, vaddr) : \
   __generic_test_and_clear_bit(nr, vaddr))

#define __test_and_clear_bit(nr,vaddr) test_and_clear_bit(nr,vaddr)

static inline int __constant_test_and_clear_bit(int nr, unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;
	char retval;

	__asm__ __volatile__ ("bclr %2,%1; sne %0"
			: "=d" (retval), "+m" (*p)
			: "di" (nr & 7));

	return retval;
}

static inline int __generic_test_and_clear_bit(int nr, unsigned long *vaddr)
{
	char retval;

	__asm__ __volatile__ ("bfclr %2{%1:#1}; sne %0"
			: "=d" (retval) : "d" (nr^31), "o" (*vaddr) : "memory");

	return retval;
}

/*
 * clear_bit() doesn't provide any barrier for the compiler.
 */
#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()

#define clear_bit(nr,vaddr) \
  (__builtin_constant_p(nr) ? \
   __constant_clear_bit(nr, vaddr) : \
   __generic_clear_bit(nr, vaddr))
#define __clear_bit(nr,vaddr) clear_bit(nr,vaddr)

static inline void __constant_clear_bit(int nr, volatile unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;
	__asm__ __volatile__ ("bclr %1,%0"
			: "+m" (*p) : "di" (nr & 7));
}

static inline void __generic_clear_bit(int nr, volatile unsigned long *vaddr)
{
	__asm__ __volatile__ ("bfclr %1{%0:#1}"
			: : "d" (nr^31), "o" (*vaddr) : "memory");
}

#define test_and_change_bit(nr,vaddr) \
  (__builtin_constant_p(nr) ? \
   __constant_test_and_change_bit(nr, vaddr) : \
   __generic_test_and_change_bit(nr, vaddr))

#define __test_and_change_bit(nr,vaddr) test_and_change_bit(nr,vaddr)
#define __change_bit(nr,vaddr) change_bit(nr,vaddr)

static inline int __constant_test_and_change_bit(int nr, unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;
	char retval;

	__asm__ __volatile__ ("bchg %2,%1; sne %0"
			: "=d" (retval), "+m" (*p)
			: "di" (nr & 7));

	return retval;
}

static inline int __generic_test_and_change_bit(int nr, unsigned long *vaddr)
{
	char retval;

	__asm__ __volatile__ ("bfchg %2{%1:#1}; sne %0"
			: "=d" (retval) : "d" (nr^31), "o" (*vaddr) : "memory");

	return retval;
}

#define change_bit(nr,vaddr) \
  (__builtin_constant_p(nr) ? \
   __constant_change_bit(nr, vaddr) : \
   __generic_change_bit(nr, vaddr))

static inline void __constant_change_bit(int nr, unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;
	__asm__ __volatile__ ("bchg %1,%0"
			: "+m" (*p) : "di" (nr & 7));
}

static inline void __generic_change_bit(int nr, unsigned long *vaddr)
{
	__asm__ __volatile__ ("bfchg %1{%0:#1}"
			: : "d" (nr^31), "o" (*vaddr) : "memory");
}

static inline int test_bit(int nr, const unsigned long *vaddr)
{
	return (vaddr[nr >> 5] & (1UL << (nr & 31))) != 0;
}

static inline int find_first_zero_bit(const unsigned long *vaddr,
				      unsigned size)
{
	const unsigned long *p = vaddr;
	int res = 32;
	unsigned int words;
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

static inline int find_next_zero_bit(const unsigned long *vaddr, int size,
				     int offset)
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

static inline int find_first_bit(const unsigned long *vaddr, unsigned size)
{
	const unsigned long *p = vaddr;
	int res = 32;
	unsigned int words;
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

static inline int find_next_bit(const unsigned long *vaddr, int size,
				int offset)
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
static inline unsigned long ffz(unsigned long word)
{
	int res;

	__asm__ __volatile__ ("bfffo %1{#0,#0},%0"
			      : "=d" (res) : "d" (~word & -~word));
	return res ^ 31;
}

#ifdef __KERNEL__

/*
 * ffs: find first bit set. This is defined the same way as
 * the libc and compiler builtin ffs routines, therefore
 * differs in spirit from the above ffz (man ffs).
 */

static inline int ffs(int x)
{
	int cnt;

	asm ("bfffo %1{#0:#0},%0" : "=d" (cnt) : "dm" (x & -x));

	return 32 - cnt;
}
#define __ffs(x) (ffs(x) - 1)

/*
 * fls: find last bit set.
 */

static inline int fls(int x)
{
	int cnt;

	asm ("bfffo %1{#0,#0},%0" : "=d" (cnt) : "dm" (x));

	return 32 - cnt;
}

static inline int __fls(int x)
{
	return fls(x) - 1;
}

#include <asm-generic/bitops/fls64.h>
#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/lock.h>

/* Bitmap functions for the little endian bitmap. */

static inline void __set_bit_le(int nr, void *addr)
{
	__set_bit(nr ^ 24, addr);
}

static inline void __clear_bit_le(int nr, void *addr)
{
	__clear_bit(nr ^ 24, addr);
}

static inline int __test_and_set_bit_le(int nr, void *addr)
{
	return __test_and_set_bit(nr ^ 24, addr);
}

static inline int test_and_set_bit_le(int nr, void *addr)
{
	return test_and_set_bit(nr ^ 24, addr);
}

static inline int __test_and_clear_bit_le(int nr, void *addr)
{
	return __test_and_clear_bit(nr ^ 24, addr);
}

static inline int test_and_clear_bit_le(int nr, void *addr)
{
	return test_and_clear_bit(nr ^ 24, addr);
}

static inline int test_bit_le(int nr, const void *vaddr)
{
	const unsigned char *p = vaddr;
	return (p[nr >> 3] & (1U << (nr & 7))) != 0;
}

static inline int find_first_zero_bit_le(const void *vaddr, unsigned size)
{
	const unsigned long *p = vaddr, *addr = vaddr;
	int res = 0;
	unsigned int words;

	if (!size)
		return 0;

	words = (size >> 5) + ((size & 31) > 0);
	while (*p++ == ~0UL) {
		if (--words == 0)
			goto out;
	}

	--p;
	for (res = 0; res < 32; res++)
		if (!test_bit_le(res, p))
			break;
out:
	res += (p - addr) * 32;
	return res < size ? res : size;
}
#define find_first_zero_bit_le find_first_zero_bit_le

static inline unsigned long find_next_zero_bit_le(const void *addr,
		unsigned long size, unsigned long offset)
{
	const unsigned long *p = addr;
	int bit = offset & 31UL, res;

	if (offset >= size)
		return size;

	p += offset >> 5;

	if (bit) {
		offset -= bit;
		/* Look for zero in first longword */
		for (res = bit; res < 32; res++)
			if (!test_bit_le(res, p)) {
				offset += res;
				return offset < size ? offset : size;
			}
		p++;
		offset += 32;

		if (offset >= size)
			return size;
	}
	/* No zero yet, search remaining full bytes for a zero */
	return offset + find_first_zero_bit_le(p, size - offset);
}
#define find_next_zero_bit_le find_next_zero_bit_le

static inline int find_first_bit_le(const void *vaddr, unsigned size)
{
	const unsigned long *p = vaddr, *addr = vaddr;
	int res = 0;
	unsigned int words;

	if (!size)
		return 0;

	words = (size >> 5) + ((size & 31) > 0);
	while (*p++ == 0UL) {
		if (--words == 0)
			goto out;
	}

	--p;
	for (res = 0; res < 32; res++)
		if (test_bit_le(res, p))
			break;
out:
	res += (p - addr) * 32;
	return res < size ? res : size;
}
#define find_first_bit_le find_first_bit_le

static inline unsigned long find_next_bit_le(const void *addr,
		unsigned long size, unsigned long offset)
{
	const unsigned long *p = addr;
	int bit = offset & 31UL, res;

	if (offset >= size)
		return size;

	p += offset >> 5;

	if (bit) {
		offset -= bit;
		/* Look for one in first longword */
		for (res = bit; res < 32; res++)
			if (test_bit_le(res, p)) {
				offset += res;
				return offset < size ? offset : size;
			}
		p++;
		offset += 32;

		if (offset >= size)
			return size;
	}
	/* No set bit yet, search remaining full bytes for a set bit */
	return offset + find_first_bit_le(p, size - offset);
}
#define find_next_bit_le find_next_bit_le

/* Bitmap functions for the ext2 filesystem. */

#define ext2_set_bit_atomic(lock, nr, addr)	\
	test_and_set_bit_le(nr, addr)
#define ext2_clear_bit_atomic(lock, nr, addr)	\
	test_and_clear_bit_le(nr, addr)

#endif /* __KERNEL__ */

#endif /* _M68K_BITOPS_H */
