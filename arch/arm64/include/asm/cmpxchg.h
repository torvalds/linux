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
#include <linux/mmdebug.h>

#include <asm/atomic.h>
#include <asm/barrier.h>
#include <asm/lse.h>

static inline unsigned long __xchg(unsigned long x, volatile void *ptr, int size)
{
	unsigned long ret, tmp;

	switch (size) {
	case 1:
		asm volatile(ARM64_LSE_ATOMIC_INSN(
		/* LL/SC */
		"1:	ldxrb	%w0, %2\n"
		"	stlxrb	%w1, %w3, %2\n"
		"	cbnz	%w1, 1b\n"
		"	dmb	ish",
		/* LSE atomics */
		"	nop\n"
		"	swpalb	%w3, %w0, %2\n"
		"	nop\n"
		"	nop")
			: "=&r" (ret), "=&r" (tmp), "+Q" (*(u8 *)ptr)
			: "r" (x)
			: "memory");
		break;
	case 2:
		asm volatile(ARM64_LSE_ATOMIC_INSN(
		/* LL/SC */
		"1:	ldxrh	%w0, %2\n"
		"	stlxrh	%w1, %w3, %2\n"
		"	cbnz	%w1, 1b\n"
		"	dmb	ish",
		/* LSE atomics */
		"	nop\n"
		"	swpalh	%w3, %w0, %2\n"
		"	nop\n"
		"	nop")
			: "=&r" (ret), "=&r" (tmp), "+Q" (*(u16 *)ptr)
			: "r" (x)
			: "memory");
		break;
	case 4:
		asm volatile(ARM64_LSE_ATOMIC_INSN(
		/* LL/SC */
		"1:	ldxr	%w0, %2\n"
		"	stlxr	%w1, %w3, %2\n"
		"	cbnz	%w1, 1b\n"
		"	dmb	ish",
		/* LSE atomics */
		"	nop\n"
		"	swpal	%w3, %w0, %2\n"
		"	nop\n"
		"	nop")
			: "=&r" (ret), "=&r" (tmp), "+Q" (*(u32 *)ptr)
			: "r" (x)
			: "memory");
		break;
	case 8:
		asm volatile(ARM64_LSE_ATOMIC_INSN(
		/* LL/SC */
		"1:	ldxr	%0, %2\n"
		"	stlxr	%w1, %3, %2\n"
		"	cbnz	%w1, 1b\n"
		"	dmb	ish",
		/* LSE atomics */
		"	nop\n"
		"	swpal	%3, %0, %2\n"
		"	nop\n"
		"	nop")
			: "=&r" (ret), "=&r" (tmp), "+Q" (*(u64 *)ptr)
			: "r" (x)
			: "memory");
		break;
	default:
		BUILD_BUG();
	}

	return ret;
}

#define xchg(ptr,x) \
({ \
	__typeof__(*(ptr)) __ret; \
	__ret = (__typeof__(*(ptr))) \
		__xchg((unsigned long)(x), (ptr), sizeof(*(ptr))); \
	__ret; \
})

static inline unsigned long __cmpxchg(volatile void *ptr, unsigned long old,
				      unsigned long new, int size)
{
	switch (size) {
	case 1:
		return __cmpxchg_case_1(ptr, old, new);
	case 2:
		return __cmpxchg_case_2(ptr, old, new);
	case 4:
		return __cmpxchg_case_4(ptr, old, new);
	case 8:
		return __cmpxchg_case_8(ptr, old, new);
	default:
		BUILD_BUG();
	}

	unreachable();
}

#define system_has_cmpxchg_double()     1

static inline int __cmpxchg_double(volatile void *ptr1, volatile void *ptr2,
		unsigned long old1, unsigned long old2,
		unsigned long new1, unsigned long new2, int size)
{
	unsigned long loop, lost;

	switch (size) {
	case 8:
		VM_BUG_ON((unsigned long *)ptr2 - (unsigned long *)ptr1 != 1);
		do {
			asm volatile("// __cmpxchg_double8\n"
			"	ldxp	%0, %1, %2\n"
			"	eor	%0, %0, %3\n"
			"	eor	%1, %1, %4\n"
			"	orr	%1, %0, %1\n"
			"	mov	%w0, #0\n"
			"	cbnz	%1, 1f\n"
			"	stxp	%w0, %5, %6, %2\n"
			"1:\n"
				: "=&r"(loop), "=&r"(lost), "+Q" (*(u64 *)ptr1)
				: "r" (old1), "r"(old2), "r"(new1), "r"(new2));
		} while (loop);
		break;
	default:
		BUILD_BUG();
	}

	return !lost;
}

static inline int __cmpxchg_double_mb(volatile void *ptr1, volatile void *ptr2,
			unsigned long old1, unsigned long old2,
			unsigned long new1, unsigned long new2, int size)
{
	int ret;

	smp_mb();
	ret = __cmpxchg_double(ptr1, ptr2, old1, old2, new1, new2, size);
	smp_mb();

	return ret;
}

static inline unsigned long __cmpxchg_mb(volatile void *ptr, unsigned long old,
					 unsigned long new, int size)
{
	switch (size) {
	case 1:
		return __cmpxchg_case_mb_1(ptr, old, new);
	case 2:
		return __cmpxchg_case_mb_2(ptr, old, new);
	case 4:
		return __cmpxchg_case_mb_4(ptr, old, new);
	case 8:
		return __cmpxchg_case_mb_8(ptr, old, new);
	default:
		BUILD_BUG();
	}

	unreachable();
}

#define cmpxchg(ptr, o, n) \
({ \
	__typeof__(*(ptr)) __ret; \
	__ret = (__typeof__(*(ptr))) \
		__cmpxchg_mb((ptr), (unsigned long)(o), (unsigned long)(n), \
			     sizeof(*(ptr))); \
	__ret; \
})

#define cmpxchg_local(ptr, o, n) \
({ \
	__typeof__(*(ptr)) __ret; \
	__ret = (__typeof__(*(ptr))) \
		__cmpxchg((ptr), (unsigned long)(o), \
			  (unsigned long)(n), sizeof(*(ptr))); \
	__ret; \
})

#define cmpxchg_double(ptr1, ptr2, o1, o2, n1, n2) \
({\
	int __ret;\
	__ret = __cmpxchg_double_mb((ptr1), (ptr2), (unsigned long)(o1), \
			(unsigned long)(o2), (unsigned long)(n1), \
			(unsigned long)(n2), sizeof(*(ptr1)));\
	__ret; \
})

#define cmpxchg_double_local(ptr1, ptr2, o1, o2, n1, n2) \
({\
	int __ret;\
	__ret = __cmpxchg_double((ptr1), (ptr2), (unsigned long)(o1), \
			(unsigned long)(o2), (unsigned long)(n1), \
			(unsigned long)(n2), sizeof(*(ptr1)));\
	__ret; \
})

#define _protect_cmpxchg_local(pcp, o, n)			\
({								\
	typeof(*raw_cpu_ptr(&(pcp))) __ret;			\
	preempt_disable();					\
	__ret = cmpxchg_local(raw_cpu_ptr(&(pcp)), o, n);	\
	preempt_enable();					\
	__ret;							\
})

#define this_cpu_cmpxchg_1(ptr, o, n) _protect_cmpxchg_local(ptr, o, n)
#define this_cpu_cmpxchg_2(ptr, o, n) _protect_cmpxchg_local(ptr, o, n)
#define this_cpu_cmpxchg_4(ptr, o, n) _protect_cmpxchg_local(ptr, o, n)
#define this_cpu_cmpxchg_8(ptr, o, n) _protect_cmpxchg_local(ptr, o, n)

#define this_cpu_cmpxchg_double_8(ptr1, ptr2, o1, o2, n1, n2)		\
({									\
	int __ret;							\
	preempt_disable();						\
	__ret = cmpxchg_double_local(	raw_cpu_ptr(&(ptr1)),		\
					raw_cpu_ptr(&(ptr2)),		\
					o1, o2, n1, n2);		\
	preempt_enable();						\
	__ret;								\
})

#define cmpxchg64(ptr,o,n)		cmpxchg((ptr),(o),(n))
#define cmpxchg64_local(ptr,o,n)	cmpxchg_local((ptr),(o),(n))

#define cmpxchg64_relaxed(ptr,o,n)	cmpxchg_local((ptr),(o),(n))

#endif	/* __ASM_CMPXCHG_H */
