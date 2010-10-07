/* system.h: FR-V CPU control definitions
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_SYSTEM_H
#define _ASM_SYSTEM_H

#include <linux/types.h>
#include <linux/linkage.h>
#include <linux/kernel.h>

struct thread_struct;

/*
 * switch_to(prev, next) should switch from task `prev' to `next'
 * `prev' will never be the same as `next'.
 * The `mb' is to tell GCC not to cache `current' across this call.
 */
extern asmlinkage
struct task_struct *__switch_to(struct thread_struct *prev_thread,
				struct thread_struct *next_thread,
				struct task_struct *prev);

#define switch_to(prev, next, last)					\
do {									\
	(prev)->thread.sched_lr =					\
		(unsigned long) __builtin_return_address(0);		\
	(last) = __switch_to(&(prev)->thread, &(next)->thread, (prev));	\
	mb();								\
} while(0)

/*
 * Force strict CPU ordering.
 */
#define nop()			asm volatile ("nop"::)
#define mb()			asm volatile ("membar" : : :"memory")
#define rmb()			asm volatile ("membar" : : :"memory")
#define wmb()			asm volatile ("membar" : : :"memory")
#define read_barrier_depends()	do { } while (0)

#ifdef CONFIG_SMP
#define smp_mb()			mb()
#define smp_rmb()			rmb()
#define smp_wmb()			wmb()
#define smp_read_barrier_depends()	read_barrier_depends()
#define set_mb(var, value) \
	do { xchg(&var, (value)); } while (0)
#else
#define smp_mb()			barrier()
#define smp_rmb()			barrier()
#define smp_wmb()			barrier()
#define smp_read_barrier_depends()	do {} while(0)
#define set_mb(var, value) \
	do { var = (value); barrier(); } while (0)
#endif

extern void die_if_kernel(const char *, ...) __attribute__((format(printf, 1, 2)));
extern void free_initmem(void);

#define arch_align_stack(x) (x)

/*****************************************************************************/
/*
 * compare and conditionally exchange value with memory
 * - if (*ptr == test) then orig = *ptr; *ptr = test;
 * - if (*ptr != test) then orig = *ptr;
 */
extern uint64_t __cmpxchg_64(uint64_t test, uint64_t new, volatile uint64_t *v);

#ifndef CONFIG_FRV_OUTOFLINE_ATOMIC_OPS

#define cmpxchg(ptr, test, new)							\
({										\
	__typeof__(ptr) __xg_ptr = (ptr);					\
	__typeof__(*(ptr)) __xg_orig, __xg_tmp;					\
	__typeof__(*(ptr)) __xg_test = (test);					\
	__typeof__(*(ptr)) __xg_new = (new);					\
										\
	switch (sizeof(__xg_orig)) {						\
	case 4:									\
		asm volatile(							\
			"0:						\n"	\
			"	orcc		gr0,gr0,gr0,icc3	\n"	\
			"	ckeq		icc3,cc7		\n"	\
			"	ld.p		%M0,%1			\n"	\
			"	orcr		cc7,cc7,cc3		\n"	\
			"	sub%I4cc	%1,%4,%2,icc0		\n"	\
			"	bne		icc0,#0,1f		\n"	\
			"	cst.p		%3,%M0		,cc3,#1	\n"	\
			"	corcc		gr29,gr29,gr0	,cc3,#1	\n"	\
			"	beq		icc3,#0,0b		\n"	\
			"1:						\n"	\
			: "+U"(*__xg_ptr), "=&r"(__xg_orig), "=&r"(__xg_tmp)	\
			: "r"(__xg_new), "NPr"(__xg_test)			\
			: "memory", "cc7", "cc3", "icc3", "icc0"		\
			);							\
		break;								\
										\
	default:								\
		__xg_orig = (__typeof__(__xg_orig))0;				\
		asm volatile("break");						\
		break;								\
	}									\
										\
	__xg_orig;								\
})

#else

extern uint32_t __cmpxchg_32(uint32_t *v, uint32_t test, uint32_t new);

#define cmpxchg(ptr, test, new)							\
({										\
	__typeof__(ptr) __xg_ptr = (ptr);					\
	__typeof__(*(ptr)) __xg_orig;						\
	__typeof__(*(ptr)) __xg_test = (test);					\
	__typeof__(*(ptr)) __xg_new = (new);					\
										\
	switch (sizeof(__xg_orig)) {						\
	case 4: __xg_orig = (__force __typeof__(*ptr))				\
			__cmpxchg_32((__force uint32_t *)__xg_ptr,		\
					 (__force uint32_t)__xg_test,		\
					 (__force uint32_t)__xg_new); break;	\
	default:								\
		__xg_orig = (__typeof__(__xg_orig))0;				\
		asm volatile("break");						\
		break;								\
	}									\
										\
	__xg_orig;								\
})

#endif

#include <asm-generic/cmpxchg-local.h>

static inline unsigned long __cmpxchg_local(volatile void *ptr,
				      unsigned long old,
				      unsigned long new, int size)
{
	switch (size) {
	case 4:
		return cmpxchg((unsigned long *)ptr, old, new);
	default:
		return __cmpxchg_local_generic(ptr, old, new, size);
	}

	return old;
}

/*
 * cmpxchg_local and cmpxchg64_local are atomic wrt current CPU. Always make
 * them available.
 */
#define cmpxchg_local(ptr, o, n)				  	\
	((__typeof__(*(ptr)))__cmpxchg_local((ptr), (unsigned long)(o),	\
			(unsigned long)(n), sizeof(*(ptr))))
#define cmpxchg64_local(ptr, o, n) __cmpxchg64_local_generic((ptr), (o), (n))

#endif /* _ASM_SYSTEM_H */
