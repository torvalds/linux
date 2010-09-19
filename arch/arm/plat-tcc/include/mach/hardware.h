/*
 * Author: RidgeRun, Inc. Greg Lonnon <glonnon@ridgerun.com>
 * Reorganized for Linux-2.6 by Tony Lindgren <tony@atomide.com>
 *                          and Dirk Behme <dirk.behme@de.bosch.com>
 * Rewritten by:    <linux@telechips.com>
 * Description: Hardware definitions for TCC8300 processors and boards
 *
 * Copyright (C) 2001 RidgeRun, Inc.
 * Copyright (C) 2008-2009 Telechips
 *
 * Modifications for mainline (C) 2009 Hans J. Koch <hjk@linutronix.de>
 *
 * Licensed under the terms of the GNU Pulic License version 2.
 */

#ifndef __ASM_ARCH_TCC_HARDWARE_H
#define __ASM_ARCH_TCC_HARDWARE_H

#include <asm/sizes.h>
#ifndef __ASSEMBLER__
#include <asm/types.h>
#endif
#include <mach/io.h>

/*
 * ----------------------------------------------------------------------------
 * Clocks
 * ----------------------------------------------------------------------------
 */
#define CLKGEN_REG_BASE		0xfffece00
#define ARM_CKCTL		(CLKGEN_REG_BASE + 0x0)
#define ARM_IDLECT1		(CLKGEN_REG_BASE + 0x4)
#define ARM_IDLECT2		(CLKGEN_REG_BASE + 0x8)
#define ARM_EWUPCT		(CLKGEN_REG_BASE + 0xC)
#define ARM_RSTCT1		(CLKGEN_REG_BASE + 0x10)
#define ARM_RSTCT2		(CLKGEN_REG_BASE + 0x14)
#define ARM_SYSST		(CLKGEN_REG_BASE + 0x18)
#define ARM_IDLECT3		(CLKGEN_REG_BASE + 0x24)

/* DPLL control registers */
#define DPLL_CTL		0xfffecf00

#endif	/* __ASM_ARCH_TCC_HARDWARE_H */
