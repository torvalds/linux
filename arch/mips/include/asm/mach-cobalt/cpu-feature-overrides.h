/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006, 07 Ralf Baechle (ralf@linux-mips.org)
 */
#ifndef __ASM_COBALT_CPU_FEATURE_OVERRIDES_H
#define __ASM_COBALT_CPU_FEATURE_OVERRIDES_H


#define cpu_has_tlb		1
#define cpu_has_4kex		1
#define cpu_has_3k_cache	0
#define cpu_has_4k_cache	1
#define cpu_has_tx39_cache	0
#define cpu_has_32fpr		1
#define cpu_has_counter		1
#define cpu_has_watch		0
#define cpu_has_divec		1
#define cpu_has_vce		0
#define cpu_has_cache_cdex_p	0
#define cpu_has_cache_cdex_s	0
#define cpu_has_prefetch	0
#define cpu_has_mcheck		0
#define cpu_has_ejtag		0

#define cpu_has_inclusive_pcaches	0
#define cpu_dcache_line_size()	32
#define cpu_icache_line_size()	32
#define cpu_scache_line_size()	0

#ifdef CONFIG_64BIT
#define cpu_has_llsc		0
#else
#define cpu_has_llsc		1
#endif

#define cpu_has_mips16		0
#define cpu_has_mips16e2	0
#define cpu_has_mdmx		0
#define cpu_has_mips3d		0
#define cpu_has_smartmips	0
#define cpu_has_vtag_icache	0
#define cpu_has_ic_fills_f_dc	0
#define cpu_icache_snoops_remote_store	0
#define cpu_has_dsp		0
#define cpu_has_dsp2		0
#define cpu_has_mipsmt		0
#define cpu_has_userlocal	0

#define cpu_has_mips32r1	0
#define cpu_has_mips32r2	0
#define cpu_has_mips64r1	0
#define cpu_has_mips64r2	0

#endif /* __ASM_COBALT_CPU_FEATURE_OVERRIDES_H */
