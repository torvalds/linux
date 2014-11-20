/*
 * Copyright (C) 1999, 2000  Niibe Yutaka  &  Kaz Kojima
 * Copyright (C) 2002 Paul Mundt
 */
#ifndef __ASM_SH_BARRIER_H
#define __ASM_SH_BARRIER_H

#if defined(CONFIG_CPU_SH4A) || defined(CONFIG_CPU_SH5)
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
#if defined(CONFIG_CPU_SH4A) || defined(CONFIG_CPU_SH5)
#define mb()		__asm__ __volatile__ ("synco": : :"memory")
#define rmb()		mb()
#define wmb()		mb()
#define ctrl_barrier()	__icbi(PAGE_OFFSET)
#else
#define ctrl_barrier()	__asm__ __volatile__ ("nop;nop;nop;nop;nop;nop;nop;nop")
#endif

#define set_mb(var, value) do { (void)xchg(&var, value); } while (0)

#include <asm-generic/barrier.h>

#endif /* __ASM_SH_BARRIER_H */
