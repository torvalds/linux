/*
 * Joshua Henderson <joshua.henderson@microchip.com>
 * Copyright (C) 2015 Microchip Technology Inc.  All rights reserved.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_MACH_PIC32_CPU_FEATURE_OVERRIDES_H
#define __ASM_MACH_PIC32_CPU_FEATURE_OVERRIDES_H

/*
 * CPU feature overrides for PIC32 boards
 */
#ifdef CONFIG_CPU_MIPS32
#define cpu_has_vint		1
#define cpu_has_veic		0
#define cpu_has_tlb		1
#define cpu_has_4kex		1
#define cpu_has_4k_cache	1
#define cpu_has_fpu		0
#define cpu_has_counter		1
#define cpu_has_llsc		1
#define cpu_has_nofpuex		0
#define cpu_icache_snoops_remote_store 1
#endif

#ifdef CONFIG_CPU_MIPS64
#error This platform does not support 64bit.
#endif

#endif /* __ASM_MACH_PIC32_CPU_FEATURE_OVERRIDES_H */
