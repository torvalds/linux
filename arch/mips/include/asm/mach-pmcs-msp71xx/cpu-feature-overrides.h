/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003, 04, 07 Ralf Baechle (ralf@linux-mips.org)
 */
#ifndef __ASM_MACH_MSP71XX_CPU_FEATURE_OVERRIDES_H
#define __ASM_MACH_MSP71XX_CPU_FEATURE_OVERRIDES_H

#define cpu_has_mips16		1
#define cpu_has_dsp		1
/* #define cpu_has_dsp2		??? - do runtime detection */
#define cpu_has_mipsmt		1
#define cpu_has_fpu		0

#define cpu_has_mips32r1	0
#define cpu_has_mips32r2	1
#define cpu_has_mips64r1	0
#define cpu_has_mips64r2	0

#endif /* __ASM_MACH_MSP71XX_CPU_FEATURE_OVERRIDES_H */
