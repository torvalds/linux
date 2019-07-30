/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/barrier.h
 *
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_BARRIER_H
#define __ASM_BARRIER_H

#ifndef __ASSEMBLY__

#include <linux/kasan-checks.h>

#define __nops(n)	".rept	" #n "\nnop\n.endr\n"
#define nops(n)		asm volatile(__nops(n))

#define sev()		asm volatile("sev" : : : "memory")
#define wfe()		asm volatile("wfe" : : : "memory")
#define wfi()		asm volatile("wfi" : : : "memory")

#define isb()		asm volatile("isb" : : : "memory")
#define dmb(opt)	asm volatile("dmb " #opt : : : "memory")
#define dsb(opt)	asm volatile("dsb " #opt : : : "memory")

#define psb_csync()	asm volatile("hint #17" : : : "memory")
#define csdb()		asm volatile("hint #20" : : : "memory")

#define spec_bar()	asm volatile(ALTERNATIVE("dsb nsh\nisb\n",		\
						 SB_BARRIER_INSN"nop\n",	\
						 ARM64_HAS_SB))

#define mb()		dsb(sy)
#define rmb()		dsb(ld)
#define wmb()		dsb(st)

#define dma_rmb()	dmb(oshld)
#define dma_wmb()	dmb(oshst)

/*
 * Generate a mask for array_index__nospec() that is ~0UL when 0 <= idx < sz
 * and 0 otherwise.
 */
#define array_index_mask_nospec array_index_mask_nospec
static inline unsigned long array_index_mask_nospec(unsigned long idx,
						    unsigned long sz)
{
	unsigned long mask;

	asm volatile(
	"	cmp	%1, %2\n"
	"	sbc	%0, xzr, xzr\n"
	: "=r" (mask)
	: "r" (idx), "Ir" (sz)
	: "cc");

	csdb();
	return mask;
}

#define __smp_mb()	dmb(ish)
#define __smp_rmb()	dmb(ishld)
#define __smp_wmb()	dmb(ishst)

#define __smp_store_release(p, v)					\
do {									\
	typeof(p) __p = (p);						\
	union { typeof(*p) __val; char __c[1]; } __u =			\
		{ .__val = (__force typeof(*p)) (v) };			\
	compiletime_assert_atomic_type(*p);				\
	kasan_check_write(__p, sizeof(*p));				\
	switch (sizeof(*p)) {						\
	case 1:								\
		asm volatile ("stlrb %w1, %0"				\
				: "=Q" (*__p)				\
				: "r" (*(__u8 *)__u.__c)		\
				: "memory");				\
		break;							\
	case 2:								\
		asm volatile ("stlrh %w1, %0"				\
				: "=Q" (*__p)				\
				: "r" (*(__u16 *)__u.__c)		\
				: "memory");				\
		break;							\
	case 4:								\
		asm volatile ("stlr %w1, %0"				\
				: "=Q" (*__p)				\
				: "r" (*(__u32 *)__u.__c)		\
				: "memory");				\
		break;							\
	case 8:								\
		asm volatile ("stlr %1, %0"				\
				: "=Q" (*__p)				\
				: "r" (*(__u64 *)__u.__c)		\
				: "memory");				\
		break;							\
	}								\
} while (0)

#define __smp_load_acquire(p)						\
({									\
	union { typeof(*p) __val; char __c[1]; } __u;			\
	typeof(p) __p = (p);						\
	compiletime_assert_atomic_type(*p);				\
	kasan_check_read(__p, sizeof(*p));				\
	switch (sizeof(*p)) {						\
	case 1:								\
		asm volatile ("ldarb %w0, %1"				\
			: "=r" (*(__u8 *)__u.__c)			\
			: "Q" (*__p) : "memory");			\
		break;							\
	case 2:								\
		asm volatile ("ldarh %w0, %1"				\
			: "=r" (*(__u16 *)__u.__c)			\
			: "Q" (*__p) : "memory");			\
		break;							\
	case 4:								\
		asm volatile ("ldar %w0, %1"				\
			: "=r" (*(__u32 *)__u.__c)			\
			: "Q" (*__p) : "memory");			\
		break;							\
	case 8:								\
		asm volatile ("ldar %0, %1"				\
			: "=r" (*(__u64 *)__u.__c)			\
			: "Q" (*__p) : "memory");			\
		break;							\
	}								\
	__u.__val;							\
})

#define smp_cond_load_relaxed(ptr, cond_expr)				\
({									\
	typeof(ptr) __PTR = (ptr);					\
	typeof(*ptr) VAL;						\
	for (;;) {							\
		VAL = READ_ONCE(*__PTR);				\
		if (cond_expr)						\
			break;						\
		__cmpwait_relaxed(__PTR, VAL);				\
	}								\
	VAL;								\
})

#define smp_cond_load_acquire(ptr, cond_expr)				\
({									\
	typeof(ptr) __PTR = (ptr);					\
	typeof(*ptr) VAL;						\
	for (;;) {							\
		VAL = smp_load_acquire(__PTR);				\
		if (cond_expr)						\
			break;						\
		__cmpwait_relaxed(__PTR, VAL);				\
	}								\
	VAL;								\
})

#include <asm-generic/barrier.h>

#endif	/* __ASSEMBLY__ */

#endif	/* __ASM_BARRIER_H */
