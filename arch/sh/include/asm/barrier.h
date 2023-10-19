/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 1999, 2000  Niibe Yutaka  &  Kaz Kojima
 * Copyright (C) 2002 Paul Mundt
 */
#ifndef __ASM_SH_BARRIER_H
#define __ASM_SH_BARRIER_H

#if defined(CONFIG_CPU_SH4A)
#include <asm/cache_insns.h>
#endif

/*
 * A brief note on ctrl_barrier(), the control register write barrier.
 *
 * Legacy SH cores typically require a sequence of 8 nops after
 * modification of a control register in order for the changes to take
 * effect. On newer cores (like the sh4a and sh5) this is accomplished
 * with icbi.
 *
 * Also note that on sh4a in the icbi case we can forego a synco for the
 * write barrier, as it's not necessary for control registers.
 *
 * Historically we have only done this type of barrier for the MMUCR, but
 * it's also necessary for the CCR, so we make it generic here instead.
 */
#if defined(CONFIG_CPU_SH4A)
#define mb()		__asm__ __volatile__ ("synco": : :"memory")
#define rmb()		mb()
#define wmb()		mb()
#define ctrl_barrier()	__icbi(PAGE_OFFSET)
#else
#if defined(CONFIG_CPU_J2) && defined(CONFIG_SMP)
#define __smp_mb()	do { int tmp = 0; __asm__ __volatile__ ("cas.l %0,%0,@%1" : "+r"(tmp) : "z"(&tmp) : "memory", "t"); } while(0)
#define __smp_rmb()	__smp_mb()
#define __smp_wmb()	__smp_mb()
#endif
#define ctrl_barrier()	__asm__ __volatile__ ("nop;nop;nop;nop;nop;nop;nop;nop")
#endif

#define __smp_store_mb(var, value) do { (void)xchg(&var, value); } while (0)

#include <asm-generic/barrier.h>

#endif /* __ASM_SH_BARRIER_H */
