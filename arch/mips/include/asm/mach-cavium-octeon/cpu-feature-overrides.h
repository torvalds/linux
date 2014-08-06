/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004 Cavium Networks
 */
#ifndef __ASM_MACH_CAVIUM_OCTEON_CPU_FEATURE_OVERRIDES_H
#define __ASM_MACH_CAVIUM_OCTEON_CPU_FEATURE_OVERRIDES_H

#include <linux/types.h>
#include <asm/mipsregs.h>

/*
 * Cavium Octeons are MIPS64v2 processors
 */
#define cpu_dcache_line_size()	128
#define cpu_icache_line_size()	128


#define cpu_has_4kex		1
#define cpu_has_3k_cache	0
#define cpu_has_4k_cache	0
#define cpu_has_tx39_cache	0
#define cpu_has_counter		1
#define cpu_has_watch		1
#define cpu_has_divec		1
#define cpu_has_vce		0
#define cpu_has_cache_cdex_p	0
#define cpu_has_cache_cdex_s	0
#define cpu_has_prefetch	1

#define cpu_has_llsc		1
/*
 * We Disable LL/SC on non SMP systems as it is faster to disable
 * interrupts for atomic access than a LL/SC.
 */
#ifdef CONFIG_SMP
# define kernel_uses_llsc	1
#else
# define kernel_uses_llsc	0
#endif
#define cpu_has_vtag_icache	1
#define cpu_has_dc_aliases	0
#define cpu_has_ic_fills_f_dc	0
#define cpu_has_64bits		1
#define cpu_has_octeon_cache	1
#define cpu_has_saa		octeon_has_saa()
#define cpu_has_mips32r1	0
#define cpu_has_mips32r2	0
#define cpu_has_mips64r1	0
#define cpu_has_mips64r2	1
#define cpu_has_mips_r2_exec_hazard 0
#define cpu_has_dsp		0
#define cpu_has_dsp2		0
#define cpu_has_mipsmt		0
#define cpu_has_vint		0
#define cpu_has_veic		0
#define cpu_hwrena_impl_bits	0xc0000000

#define cpu_has_rixi		(cpu_data[0].cputype != CPU_CAVIUM_OCTEON)

#define ARCH_HAS_IRQ_PER_CPU	1
#define ARCH_HAS_SPINLOCK_PREFETCH 1
#define spin_lock_prefetch(x) prefetch(x)
#define PREFETCH_STRIDE 128

#ifdef __OCTEON__
/*
 * All gcc versions that have OCTEON support define __OCTEON__ and have the
 *  __builtin_popcount support.
 */
#define ARCH_HAS_USABLE_BUILTIN_POPCOUNT 1
#endif

static inline int octeon_has_saa(void)
{
	int id;
	asm volatile ("mfc0 %0, $15,0" : "=r" (id));
	return id >= 0x000d0300;
}

/*
 * The last 256MB are reserved for device to device mappings and the
 * BAR1 hole.
 */
#define MAX_DMA32_PFN (((1ULL << 32) - (1ULL << 28)) >> PAGE_SHIFT)

#endif
