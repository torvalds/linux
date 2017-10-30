/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2009 Wu Zhangjin <wuzhangjin@gmail.com>
 * Copyright (C) 2009 Philippe Vachon <philippe@cowpig.ca>
 * Copyright (C) 2009 Zhang Le <r0bertz@gentoo.org>
 *
 * reference: /proc/cpuinfo,
 *	arch/mips/kernel/cpu-probe.c(cpu_probe_legacy),
 *	arch/mips/kernel/proc.c(show_cpuinfo),
 *	loongson2f user manual.
 */

#ifndef __ASM_MACH_LOONGSON64_CPU_FEATURE_OVERRIDES_H
#define __ASM_MACH_LOONGSON64_CPU_FEATURE_OVERRIDES_H

#define cpu_has_32fpr		1
#define cpu_has_3k_cache	0
#define cpu_has_4k_cache	1
#define cpu_has_4kex		1
#define cpu_has_64bits		1
#define cpu_has_cache_cdex_p	0
#define cpu_has_cache_cdex_s	0
#define cpu_has_counter		1
#define cpu_has_dc_aliases	(PAGE_SIZE < 0x4000)
#define cpu_has_divec		0
#define cpu_has_ejtag		0
#define cpu_has_inclusive_pcaches	1
#define cpu_has_llsc		1
#define cpu_has_mcheck		0
#define cpu_has_mdmx		0
#define cpu_has_mips16		0
#define cpu_has_mips16e2	0
#define cpu_has_mips3d		0
#define cpu_has_mipsmt		0
#define cpu_has_smartmips	0
#define cpu_has_tlb		1
#define cpu_has_tx39_cache	0
#define cpu_has_vce		0
#define cpu_has_veic		0
#define cpu_has_vint		0
#define cpu_has_vtag_icache	0
#define cpu_has_watch		1
#define cpu_has_local_ebase	0

#ifdef CONFIG_CPU_LOONGSON3
#define cpu_has_wsbh		1
#define cpu_has_ic_fills_f_dc	1
#define cpu_hwrena_impl_bits	0xc0000000
#endif

#endif /* __ASM_MACH_LOONGSON64_CPU_FEATURE_OVERRIDES_H */
