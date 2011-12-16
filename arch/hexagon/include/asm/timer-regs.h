/*
 * Timer support for Hexagon
 *
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
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
