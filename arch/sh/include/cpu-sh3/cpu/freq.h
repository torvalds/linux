/* SPDX-License-Identifier: GPL-2.0
 *
 * include/asm-sh/cpu-sh3/freq.h
 *
 * Copyright (C) 2002, 2003 Paul Mundt
 */
#ifndef __ASM_CPU_SH3_FREQ_H
#define __ASM_CPU_SH3_FREQ_H

#ifdef CONFIG_CPU_SUBTYPE_SH7712
#define FRQCR			0xA415FF80
#else
#define FRQCR			0xffffff80
#endif

#define MIN_DIVISOR_NR		0
#define MAX_DIVISOR_NR		4

#define FRQCR_CKOEN	0x0100
#define FRQCR_PLLEN	0x0080
#define FRQCR_PSTBY	0x0040

#endif /* __ASM_CPU_SH3_FREQ_H */

