/*
 *  Atheros AR71XX/AR724X/AR913X specific CPU feature overrides
 *
 *  Copyright (C) 2008-2010 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
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
#ifndef __ASM_MACH_ATH79_CPU_FEATURE_OVERRIDES_H
#define __ASM_MACH_ATH79_CPU_FEATURE_OVERRIDES_H

#define cpu_has_tlb		1
#define cpu_has_4kex		1
#define cpu_has_3k_cache	0
#define cpu_has_4k_cache	1
#define cpu_has_tx39_cache	0
#define cpu_has_sb1_cache	0
#define cpu_has_fpu		0
#define cpu_has_32fpr		0
#define cpu_has_counter		1
#define cpu_has_watch		1
#define cpu_has_divec		1

#define cpu_has_prefetch	1
#define cpu_has_ejtag		1
#define cpu_has_llsc		1

#define cpu_has_mips16		1
#define cpu_has_mdmx		0
#define cpu_has_mips3d		0
#define cpu_has_smartmips	0

#define cpu_has_mips32r1	1
#define cpu_has_mips32r2	1
#define cpu_has_mips64r1	0
#define cpu_has_mips64r2	0

#define cpu_has_mipsmt		0

#define cpu_has_64bits		0
#define cpu_has_64bit_zero_reg	0
#define cpu_has_64bit_gp_regs	0
#define cpu_has_64bit_addresses	0

#define cpu_dcache_line_size()	32
#define cpu_icache_line_size()	32

#endif /* __ASM_MACH_ATH79_CPU_FEATURE_OVERRIDES_H */
