/*
 * Based on arch/arm/include/asm/cmpxchg.h
 *
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_CMPXCHG_H
#define __ASM_CMPXCHG_H

#include <linux/bug.h>

#include <asm/barrier.h>

static inline unsigned long __xchg(unsigned long x, volatile void *ptr, int size)
{
	unsigned long ret, tmp;

	switch (size) {
	case 1:
		asm volatile("//	__xchg1\n"
		"1:	ldaxrb	%w0, [%3]\n"
		"	stlxrb	%w1, %w2, [%3]\n"
		"	cbnz	%w1, 1b\n"
			: "=&r" (ret), "=&r" (tmp)
			: "r" (x), "r" (ptr)
			: "memory", "cc");
		break;
	case 2:
		asm volatile("//	__xchg2\n"
		"1:	ldaxrh	%w0, [%3]\n"
		"	stlxrh	%w1, %w2, [%3]\n"
		"	cbnz	%w1, 1b\n"
			: "=&r" (ret), "=&r" (tmp)
			: "r" (x), "r" (ptr)
			: "memory", "cc");
		break;
	case 4:
		asm volatile("//	__xchg4\n"
		"1:	ldaxr	%w0, [%3]\n"
		"	stlxr	%w1, %w2, [%3]\n"
		"	cbnz	%w1, 1b\n"
			: "=&r" (ret), "=&r" (tmp)
			: "r" (x), "r" (ptr)
			: "memory", "cc");
		break;
	case 8:
		asm volatile("//	__xchg8\n"
		"1:	ldaxr	%0, [%3]\n"
		"	stlxr	%w1, %2, [%3]\n"
		"	cbnz	%w1, 1b\n"
			: "=&r" (ret), "=&r" (tmp)
			: "r" (x), "r" (ptr)
			: "memory", "cc");
		break;
	default:
		BUILD_BUG();
	}

	return ret;
}

#define xchg(ptr,x) \
	((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

static inline unsigned long __cmpxchg(volatile void *ptr, unsigned long old,
				      unsigned long new, int size)
{
	unsigned long oldval = 0, res;

	switch (size) {
	case 1:
		do {
			asm volatile("// __cmpxchg1\n"
			"	ldxrb	%w1, [%2]\n"
			"	mov	%w0, #0\n"
			"	cmp	%w1, %w3\n"
			"	b.ne	1f\n"
			"	stxrb	%w0, %w4, [%2]\n"
			"1:\n"
				: "=&r" (res), "=&r" (oldval)
				: "r" (ptr), "Ir" (old), "r" (new)
				: "cc");
		} while (res);
		break;

	case 2:
		do {
			asm volatile("// __cmpxchg2\n"
			"	ldxrh	%w1, [%2]\n"
			"	mov	%w0, #0\n"
			"	cmp	%w1, %w3\n"
			"	b.ne	1f\n"
			"	stxrh	%w0, %w4, [%2]\n"
			"1:\n"
				: "=&r" (res), "=&r" (oldval)
				: "r" (ptr), "Ir" (old), "r" (new)
				: "memory", "cc");
		} while (res);
		break;

	case 4:
		do {
			asm volatile("// __cmpxchg4\n"
			"	ldxr	%w1, [%2]\n"
			"	mov	%w0, #0\n"
			"	cmp	%w1, %w3\n"
			"	b.ne	1f\n"
			"	stxr	%w0, %w4, [%2]\n"
			"1:\n"
				: "=&r" (res), "=&r" (oldval)
				: "r" (ptr), "Ir" (old), "r" (new)
				: "cc");
		} while (res);
		break;

	case 8:
		do {
			asm volatile("// __cmpxchg8\n"
			"	ldxr	%1, [%2]\n"
			"	mov	%w0, #0\n"
			"	cmp	%1, %3\n"
			"	b.ne	1f\n"
			"	stxr	%w0, %4, [%2]\n"
			"1:\n"
				: "=&r" (res), "=&r" (oldval)
				: "r" (ptr), "Ir" (old), "r" (new)
				: "cc");
		} while (res);
		break;

	default:
		BUILD_BUG();
	}

	return oldval;
}

static inline unsigned long __cmpxchg_mb(volatile void *ptr, unsigned long old,
					 unsigned long new, int size)
{
	unsigned long ret;

	smp_mb();
	ret = __cmpxchg(ptr, old, new, size);
	smp_mb();

	return ret;
}

#define cmpxchg(ptr,o,n)						\
	((__typeof__(*(ptr)))__cmpxchg_mb((ptr),			\
					  (unsigned long)(o),		\
					  (unsigned long)(n),		\
					  sizeof(*(ptr))))

#define cmpxchg_local(ptr,o,n)						\
	((__typeof__(*(ptr)))__cmpxchg((ptr),				\
				       (unsigned long)(o),		\
				       (unsigned long)(n),		\
				       sizeof(*(ptr))))

#endif	/* __ASM_CMPXCHG_H */
