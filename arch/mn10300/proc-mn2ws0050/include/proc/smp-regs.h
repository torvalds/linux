/* MN10300/AM33v2 Microcontroller SMP registers
 *
 * Copyright (C) 2006 Matsushita Electric Industrial Co., Ltd.
 * All Rights Reserved.
 * Created:
 *  13-Nov-2006 MEI Add extended cache and atomic operation register
 *                  for SMP support.
 *  23-Feb-2007 MEI Add define for gdbstub SMP.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_PROC_SMP_REGS_H
#define _ASM_PROC_SMP_REGS_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__
#include <linux/types.h>
#endif
#include <asm/cpu-regs.h>

/*
 * Reference to the interrupt controllers of other CPUs
 */
#define CROSS_ICR_CPU_SHIFT	16

#define CROSS_GxICR(X, CPU)	__SYSREG(0xc4000000 + (X) * 4 + \
	((X) >= 64 && (X) < 192) * 0xf00 + ((CPU) << CROSS_ICR_CPU_SHIFT), u16)
#define CROSS_GxICR_u8(X, CPU)	__SYSREG(0xc4000000 + (X) * 4 +		\
	(((X) >= 64) && ((X) < 192)) * 0xf00 + ((CPU) << CROSS_ICR_CPU_SHIFT), u8)

/* CPU ID register */
#define CPUID		__SYSREGC(0xc0000054, u32)
#define CPUID_MASK	0x00000007	/* CPU ID mask */

/* extended cache control register */
#define ECHCTR		__SYSREG(0xc0000c20, u32)
#define ECHCTR_IBCM	0x00000001	/* instruction cache broad cast mask */
#define ECHCTR_DBCM	0x00000002	/* data cache broad cast mask */
#define ECHCTR_ISPM	0x00000004	/* instruction cache snoop mask */
#define ECHCTR_DSPM	0x00000008	/* data cache snoop mask */

#define NMIAGR		__SYSREG(0xd400013c, u16)
#define NMIAGR_GN	0x03fc

#endif /* __KERNEL__ */
#endif /* _ASM_PROC_SMP_REGS_H */
