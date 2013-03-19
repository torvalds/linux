/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Ralf Baechle
 * 6/2004	pf
 */
#ifndef __ASM_MACH_IP28_CPU_FEATURE_OVERRIDES_H
#define __ASM_MACH_IP28_CPU_FEATURE_OVERRIDES_H

/*
 * IP28 only comes with R10000 family processors all using the same config
 */
#define cpu_has_watch		1
#define cpu_has_mips16		0
#define cpu_has_divec		0
#define cpu_has_vce		0
#define cpu_has_cache_cdex_p	0
#define cpu_has_cache_cdex_s	0
#define cpu_has_prefetch	1
#define cpu_has_mcheck		0
#define cpu_has_ejtag		0

#define cpu_has_llsc		1
#define cpu_has_vtag_icache	0
#define cpu_has_dc_aliases	0 /* see probe_pcache() */
#define cpu_has_ic_fills_f_dc	0
#define cpu_has_dsp		0
#define cpu_has_dsp2		0
#define cpu_icache_snoops_remote_store	1
#define cpu_has_mipsmt		0
#define cpu_has_userlocal	0

#define cpu_has_nofpuex		0
#define cpu_has_64bits		1

#define cpu_has_4kex		1
#define cpu_has_4k_cache	1

#define cpu_has_inclusive_pcaches	1

#define cpu_dcache_line_size()	32
#define cpu_icache_line_size()	64

#define cpu_has_mips32r1	0
#define cpu_has_mips32r2	0
#define cpu_has_mips64r1	0
#define cpu_has_mips64r2	0

#endif /* __ASM_MACH_IP28_CPU_FEATURE_OVERRIDES_H */
