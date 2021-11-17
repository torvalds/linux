/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Timer support for Hexagon
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_TIMER_REGS_H
#define _ASM_TIMER_REGS_H

/*  This stuff should go into a platform specific file  */
#define TCX0_CLK_RATE		19200
#define TIMER_ENABLE		0
#define TIMER_CLR_ON_MATCH	1

/*
 * 8x50 HDD Specs 5-8.  Simulator co-sim not fixed until
 * release 1.1, and then it's "adjustable" and probably not defaulted.
 */
#define RTOS_TIMER_INT		3
#ifdef CONFIG_HEXAGON_COMET
#define RTOS_TIMER_REGS_ADDR	0xAB000000UL
#endif
#define SLEEP_CLK_RATE		32000

#endif
