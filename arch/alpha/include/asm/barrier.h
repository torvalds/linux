/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BARRIER_H
#define __BARRIER_H

#define mb()	__asm__ __volatile__("mb": : :"memory")
#define rmb()	__asm__ __volatile__("mb": : :"memory")
#define wmb()	__asm__ __volatile__("wmb": : :"memory")

#define __smp_load_acquire(p)						\
({									\
	compiletime_assert_atomic_type(*p);				\
	__READ_ONCE(*p);						\
})

#ifdef CONFIG_SMP
#define __ASM_SMP_MB	"\tmb\n"
#else
#define __ASM_SMP_MB
#endif

#include <asm-generic/barrier.h>

#endif		/* __BARRIER_H */
