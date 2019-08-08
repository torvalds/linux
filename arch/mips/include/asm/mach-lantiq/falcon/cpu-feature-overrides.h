/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Lantiq FALCON specific CPU feature overrides
 *
 *  Copyright (C) 2013 Thomas Langer, Lantiq Deutschland
 *
 *  This file was derived from: include/asm-mips/cpu-features.h
 *	Copyright (C) 2003, 2004 Ralf Baechle
 *	Copyright (C) 2004 Maciej W. Rozycki
 */
#ifndef __ASM_MACH_FALCON_CPU_FEATURE_OVERRIDES_H
#define __ASM_MACH_FALCON_CPU_FEATURE_OVERRIDES_H

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

#define cpu_has_dsp		1
#define cpu_has_mipsmt		1

#define cpu_has_vint		1
#define cpu_has_veic		1

#define cpu_has_64bits		0
#define cpu_has_64bit_zero_reg	0
#define cpu_has_64bit_gp_regs	0
#define cpu_has_64bit_addresses	0

#define cpu_dcache_line_size()	32
#define cpu_icache_line_size()	32

#endif /* __ASM_MACH_FALCON_CPU_FEATURE_OVERRIDES_H */
