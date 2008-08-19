/*
 * include/asm-sh/cpu-sh4/timer.h
 *
 * Copyright (C) 2004 Lineo Solutions, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_CPU_SH4_TIMER_H
#define __ASM_CPU_SH4_TIMER_H

/*
 * ---------------------------------------------------------------------------
 * TMU Common definitions for SH4 processors
 *	SH7750S/SH7750R
 *	SH7751/SH7751R
 *	SH7760
 *	SH-X3
 * ---------------------------------------------------------------------------
 */
#ifdef CONFIG_CPU_SUBTYPE_SHX3
#define TMU_012_BASE	0xffc10000
#define TMU_345_BASE	0xffc20000
#else
#define TMU_012_BASE	0xffd80000
#define TMU_345_BASE	0xfe100000
#endif

#define TMU_TOCR	TMU_012_BASE	/* Not supported on all CPUs */

#define TMU_012_TSTR	(TMU_012_BASE + 0x04)
#define TMU_345_TSTR	(TMU_345_BASE + 0x04)

#define TMU0_TCOR	(TMU_012_BASE + 0x08)
#define TMU0_TCNT	(TMU_012_BASE + 0x0c)
#define TMU0_TCR	(TMU_012_BASE + 0x10)

#define TMU1_TCOR       (TMU_012_BASE + 0x14)
#define TMU1_TCNT       (TMU_012_BASE + 0x18)
#define TMU1_TCR        (TMU_012_BASE + 0x1c)

#define TMU2_TCOR       (TMU_012_BASE + 0x20)
#define TMU2_TCNT       (TMU_012_BASE + 0x24)
#define TMU2_TCR	(TMU_012_BASE + 0x28)
#define TMU2_TCPR	(TMU_012_BASE + 0x2c)

#define TMU3_TCOR	(TMU_345_BASE + 0x08)
#define TMU3_TCNT	(TMU_345_BASE + 0x0c)
#define TMU3_TCR	(TMU_345_BASE + 0x10)

#define TMU4_TCOR	(TMU_345_BASE + 0x14)
#define TMU4_TCNT	(TMU_345_BASE + 0x18)
#define TMU4_TCR	(TMU_345_BASE + 0x1c)

#define TMU5_TCOR	(TMU_345_BASE + 0x20)
#define TMU5_TCNT	(TMU_345_BASE + 0x24)
#define TMU5_TCR	(TMU_345_BASE + 0x28)

#endif /* __ASM_CPU_SH4_TIMER_H */
