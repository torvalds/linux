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

#include <asm/atomic.h>
#include <asm/barrier.h>
#include <asm/lse.h>

/*
 * We need separate acquire parameters for ll/sc and lse, since the full
 * barrier case is generated as release+dmb for the former and
 * acquire+release for the latter.
 */
#define __XCHG_CASE(w, sz, name, mb, nop_lse, acq, acq_lse, rel, cl)	\
static inline unsigned long __xchg_case_##name(unsigned long x,		\
					       volatile void *ptr)	\
{									\
	unsigned long ret, tmp;						\
									\
	asm volatile(ARM64_LSE_ATOMIC_INSN(				\
	/* LL/SC */							\
	"	prfm	pstl1strm, %2\n"				\
	"1:	ld" #acq "xr" #sz "\t%" #w "0, %2\n"			\
	"	st" #rel "xr" #sz "\t%w1, %" #w "3, %2\n"		\
	"	cbnz	%w1, 1b\n"					\
	"	" #mb,							\
	/* LSE atomics */						\
	"	swp" #acq_lse #rel #sz "\t%" #w "3, %" #w "0, %2\n"	\
		__nops(3)						\
	"	" #nop_lse)						\
	: "=&r" (ret), "=&r" (tmp), "+Q" (*(unsigned long *)ptr)	\
	: "r" (x)							\
	: cl);								\
									\
	return ret;							\
}

__XCHG_CASE(w, b,     1,        ,    ,  ,  ,  ,         )
__XCHG_CASE(w, h,     2,        ,    ,  ,  ,  ,         )
__XCHG_CASE(w,  ,     4,        ,    ,  ,  ,  ,         )
__XCHG_CASE( ,  ,     8,        ,    ,  ,  ,  ,         )
__XCHG_CASE(w, b, acq_1,        ,    , a, a,  , "memory")
__XCHG_CASE(w, h, acq_2,        ,    , a, a,  , "memory")
__XCHG_CASE(w,  , acq_4,        ,    , a, a,  , "memory")
__XCHG_CASE( ,  , acq_8,        ,    , a, a,  , "memory")
__XCHG_CASE(w, b, rel_1,        ,    ,  ,  , l, "memory")
__XCHG_CASE(w, h, rel_2,        ,    ,  ,  , l, "memory")
__XCHG_CASE(w,  , rel_4,        ,    ,  ,  , l, "memory")
__XCHG_CASE( ,  , rel_8,        ,    ,  ,  , l, "memory")
__XCHG_CASE(w, b,  mb_1, dmb ish, nop,  , a, l, "memory")
__XCHG_CASE(w, h,  mb_2, dmb ish, nop,  , a, l, "memory")
__XCHG_CASE(w,  ,  mb_4, dmb ish, nop,  , a, l, "memory")
__XCHG_CASE( ,  ,  mb_8, dmb ish, nop,  , a, l, "memory")

#undef __XCHG_CASE

#define __XCHG_GEN(sfx)							\
static inline unsigned long __xchg##sfx(unsigned long x,		\
					volatile void *ptr,		\
					int size)			\
{									\
	switch (size) {							\
	case 1:								\
		return __xchg_case##sfx##_1(x, ptr);			\
	case 2:								\
		return __xchg_case##sfx##_2(x, ptr);			\
	case 4:								\
		return __xchg_case##sfx##_4(x, ptr);			\
	case 8:								\
		return __xchg_case##sfx##_8(x, ptr);			\
	default:							\
		BUILD_BUG();						\
	}								\
									\
	unreachable();							\
}

__XCHG_GEN()
__XCHG_GEN(_acq)
__XCHG_GEN(_rel)
__XCHG_GEN(_mb)

#undef __XCHG_GEN

#define __xchg_wrapper(sfx, ptr, x)					\
({									\
	__typeof__(*(ptr)) __ret;					\
	__ret = (__typeof__(*(ptr)))					\
		__xchg##sfx((unsigned long)(x), (ptr), sizeof(*(ptr))); \
	__ret;								\
})

/* xchg */
#define xchg_relaxed(...)	__xchg_wrapper(    , __VA_ARGS__)
#define xchg_acquire(...)	__xchg_wrapper(_acq, __VA_ARGS__)
#define xchg_release(...)	__xchg_wrapper(_rel, __VA_ARGS__)
#define xchg(...)		__xchg_wrapper( _mb, __VA_ARGS__)

#define __CMPXCHG_GEN(sfx)						\
static inline unsigned long __cmpxchg##sfx(volatile void *ptr,		\
					   unsigned long old,		\
					   unsigned long new,		\
					   int size)			\
{									\
	switch (size) {							\
	case 1:								\
		return __cmpxchg_case##sfx##_1(ptr, (u8)old, new);	\
	case 2:								\
		return __cmpxchg_case##sfx##_2(ptr, (u16)old, new);	\
	case 4:								\
		return __cmpxchg_case##sfx##_4(ptr, old, new);		\
	case 8:								\
		return __cmpxchg_case##sfx##_8(ptr, old, new);		\
	default:							\
		BUILD_BUG();						\
	}								\
									\
	unreachable();							\
}

__CMPXCHG_GEN()
__CMPXCHG_GEN(_acq)
__CMPXCHG_GEN(_rel)
__CMPXCHG_GEN(_mb)

#undef __CMPXCHG_GEN

#define __cmpxchg_wrapper(sfx, ptr, o, n)				\
({									\
	__typeof__(*(ptr)) __ret;					\
	__ret = (__typeof__(*(ptr)))					\
		__cmpxchg##sfx((ptr), (unsigned long)(o),		\
				(unsigned long)(n), sizeof(*(ptr)));	\
	__ret;								\
})

/* cmpxchg */
#define cmpxchg_relaxed(...)	__cmpxchg_wrapper(    , __VA_ARGS__)
#define cmpxchg_acquire(...)	__cmpxchg_wrapper(_acq, __VA_ARGS__)
#define cmpxchg_release(...)	__cmpxchg_wrapper(_rel, __VA_ARGS__)
#define cmpxchg(...)		__cmpxchg_wrapper( _mb, __VA_ARGS__)
#define cmpxchg_local		cmpxchg_relaxed

/* cmpxchg64 */
#define cmpxchg64_relaxed	cmpxchg_relaxed
#define cmpxchg64_acquire	cmpxchg_acquire
#define cmpxchg64_release	cmpxchg_release
#define cmpxchg64		cmpxchg
#define cmpxchg64_local		cmpxchg_local

/* cmpxchg_double */
#define system_has_cmpxchg_double()     1

#define __cmpxchg_double_check(ptr1, ptr2)					\
({										\
	if (sizeof(*(ptr1)) != 8)						\
		BUILD_BUG();							\
	VM_BUG_ON((unsigned long *)(ptr2) - (unsigned long *)(ptr1) != 1);	\
})

#define cmpxchg_double(ptr1, ptr2, o1, o2, n1, n2) \
({\
	int __ret;\
	__cmpxchg_double_check(ptr1, ptr2); \
	__ret = !__cmpxchg_double_mb((unsigned long)(o1), (unsigned long)(o2), \
				     (unsigned long)(n1), (unsigned long)(n2), \
				     ptr1); \
	__ret; \
})

#define cmpxchg_double_local(ptr1, ptr2, o1, o2, n1, n2) \
({\
	int __ret;\
	__cmpxchg_double_check(ptr1, ptr2); \
	__ret = !__cmpxchg_double((unsigned long)(o1), (unsigned long)(o2), \
				  (unsigned long)(n1), (unsigned long)(n2), \
				  ptr1); \
	__ret; \
})

/* this_cpu_cmpxchg */
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

#define __CMPWAIT_CASE(w, sz, name)					\
static inline void __cmpwait_case_##name(volatile void *ptr,		\
					 unsigned long val)		\
{									\
	unsigned long tmp;						\
									\
	asm volatile(							\
	"	ldxr" #sz "\t%" #w "[tmp], %[v]\n"		\
	"	eor	%" #w "[tmp], %" #w "[tmp], %" #w "[val]\n"	\
	"	cbnz	%" #w "[tmp], 1f\n"				\
	"	wfe\n"							\
	"1:"								\
	: [tmp] "=&r" (tmp), [v] "+Q" (*(unsigned long *)ptr)		\
	: [val] "r" (val));						\
}

__CMPWAIT_CASE(w, b, 1);
__CMPWAIT_CASE(w, h, 2);
__CMPWAIT_CASE(w,  , 4);
__CMPWAIT_CASE( ,  , 8);

#undef __CMPWAIT_CASE

#define __CMPWAIT_GEN(sfx)						\
static inline void __cmpwait##sfx(volatile void *ptr,			\
				  unsigned long val,			\
				  int size)				\
{									\
	switch (size) {							\
	case 1:								\
		return __cmpwait_case##sfx##_1(ptr, (u8)val);		\
	case 2:								\
		return __cmpwait_case##sfx##_2(ptr, (u16)val);		\
	case 4:								\
		return __cmpwait_case##sfx##_4(ptr, val);		\
	case 8:								\
		return __cmpwait_case##sfx##_8(ptr, val);		\
	default:							\
		BUILD_BUG();						\
	}								\
									\
	unreachable();							\
}

__CMPWAIT_GEN()

#undef __CMPWAIT_GEN

#define __cmpwait_relaxed(ptr, val) \
	__cmpwait((ptr), (unsigned long)(val), sizeof(*(ptr)))

#endif	/* __ASM_CMPXCHG_H */
