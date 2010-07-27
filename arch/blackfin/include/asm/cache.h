/*
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __ARCH_BLACKFIN_CACHE_H
#define __ARCH_BLACKFIN_CACHE_H

/*
 * Bytes per L1 cache line
 * Blackfin loads 32 bytes for cache
 */
#define L1_CACHE_SHIFT	5
#define L1_CACHE_BYTES	(1 << L1_CACHE_SHIFT)
#define SMP_CACHE_BYTES	L1_CACHE_BYTES

#define ARCH_KMALLOC_MINALIGN	L1_CACHE_BYTES

#ifdef CONFIG_SMP
#define __cacheline_aligned
#else
#define ____cacheline_aligned

/*
 * Put cacheline_aliged data to L1 data memory
 */
#ifdef CONFIG_CACHELINE_ALIGNED_L1
#define __cacheline_aligned				\
	  __attribute__((__aligned__(L1_CACHE_BYTES),	\
		__section__(".data_l1.cacheline_aligned")))
#endif

#endif

/*
 * largest L1 which this arch supports
 */
#define L1_CACHE_SHIFT_MAX	5

#if defined(CONFIG_SMP) && \
    !defined(CONFIG_BFIN_CACHE_COHERENT)
# if defined(CONFIG_BFIN_EXTMEM_ICACHEABLE) || defined(CONFIG_BFIN_L2_ICACHEABLE)
# define __ARCH_SYNC_CORE_ICACHE
# endif
# if defined(CONFIG_BFIN_EXTMEM_DCACHEABLE) || defined(CONFIG_BFIN_L2_DCACHEABLE)
# define __ARCH_SYNC_CORE_DCACHE
# endif
#ifndef __ASSEMBLY__
asmlinkage void __raw_smp_mark_barrier_asm(void);
asmlinkage void __raw_smp_check_barrier_asm(void);

static inline void smp_mark_barrier(void)
{
	__raw_smp_mark_barrier_asm();
}
static inline void smp_check_barrier(void)
{
	__raw_smp_check_barrier_asm();
}

void resync_core_dcache(void);
void resync_core_icache(void);
#endif
#endif


#endif
