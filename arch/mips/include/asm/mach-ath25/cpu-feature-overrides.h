/*
 *  Atheros AR231x/AR531x SoC specific CPU feature overrides
 *
 *  Copyright (C) 2008 Gabor Juhos <juhosg@openwrt.org>
 *
 *  This file was derived from: include/asm-mips/cpu-features.h
 *	Copyright (C) 2003, 2004 Ralf Baechle
 *	Copyright (C) 2004 Maciej W. Rozycki
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 */
#ifndef __ASM_MACH_ATH25_CPU_FEATURE_OVERRIDES_H
#define __ASM_MACH_ATH25_CPU_FEATURE_OVERRIDES_H

/*
 * The Atheros AR531x/AR231x SoCs have MIPS 4Kc/4KEc core.
 */
#define cpu_has_tlb			1
#define cpu_has_4kex			1
#define cpu_has_3k_cache		0
#define cpu_has_4k_cache		1
#define cpu_has_tx39_cache		0
#define cpu_has_sb1_cache		0
#define cpu_has_fpu			0
#define cpu_has_32fpr			0
#define cpu_has_counter			1
#define cpu_has_ejtag			1

#if !defined(CONFIG_SOC_AR5312)
#  define cpu_has_llsc			1
#else
/*
 * The MIPS 4Kc V0.9 core in the AR5312/AR2312 have problems with the
 * ll/sc instructions.
 */
#  define cpu_has_llsc			0
#endif

#define cpu_has_mips16			0
#define cpu_has_mdmx			0
#define cpu_has_mips3d			0
#define cpu_has_smartmips		0

#define cpu_has_mips32r1		1

#if !defined(CONFIG_SOC_AR5312)
#  define cpu_has_mips32r2		1
#endif

#define cpu_has_mips64r1		0
#define cpu_has_mips64r2		0

#define cpu_has_dsp			0
#define cpu_has_mipsmt			0

#define cpu_has_64bits			0
#define cpu_has_64bit_zero_reg		0
#define cpu_has_64bit_gp_regs		0
#define cpu_has_64bit_addresses		0

#endif /* __ASM_MACH_ATH25_CPU_FEATURE_OVERRIDES_H */
