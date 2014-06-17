#ifndef __BARRIER_H
#define __BARRIER_H

#include <asm/compiler.h>

#define mb()	__asm__ __volatile__("mb": : :"memory")
#define rmb()	__asm__ __volatile__("mb": : :"memory")
#define wmb()	__asm__ __volatile__("wmb": : :"memory")

#define read_barrier_depends() __asm__ __volatile__("mb": : :"memory")

#ifdef CONFIG_SMP
#define __ASM_SMP_MB	"\tmb\n"
#else
#define __ASM_SMP_MB
#endif

#include <asm-generic/barrier.h>

#endif		/* __BARRIER_H */
