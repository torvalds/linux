/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2013 Cavium, Inc.
 */
#ifndef __ASM_MACH_PARAVIRT_CPU_FEATURE_OVERRIDES_H
#define __ASM_MACH_PARAVIRT_CPU_FEATURE_OVERRIDES_H

#define cpu_has_4kex		1
#define cpu_has_3k_cache	0
#define cpu_has_tx39_cache	0
#define cpu_has_counter		1
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

#ifdef CONFIG_CPU_CAVIUM_OCTEON
#define cpu_dcache_line_size()	128
#define cpu_icache_line_size()	128
#define cpu_has_octeon_cache	1
#define cpu_has_4k_cache	0
#else
#define cpu_has_octeon_cache	0
#define cpu_has_4k_cache	1
#endif

#endif /* __ASM_MACH_PARAVIRT_CPU_FEATURE_OVERRIDES_H */
