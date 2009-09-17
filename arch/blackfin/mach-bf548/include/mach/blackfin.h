/*
 * File:         include/asm-blackfin/mach-bf548/blackfin.h
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Rev:
 *
 * Modified:
 *
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.
 * If not, write to the Free Software Foundation,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _MACH_BLACKFIN_H_
#define _MACH_BLACKFIN_H_

#include "bf548.h"
#include "anomaly.h"

#ifdef CONFIG_BF542
#include "defBF542.h"
#endif

#ifdef CONFIG_BF544
#include "defBF544.h"
#endif

#ifdef CONFIG_BF547
#include "defBF547.h"
#endif

#ifdef CONFIG_BF548
#include "defBF548.h"
#endif

#ifdef CONFIG_BF549
#include "defBF549.h"
#endif

#if !defined(__ASSEMBLY__)
#ifdef CONFIG_BF542
#include "cdefBF542.h"
#endif
#ifdef CONFIG_BF544
#include "cdefBF544.h"
#endif
#ifdef CONFIG_BF547
#include "cdefBF547.h"
#endif
#ifdef CONFIG_BF548
#include "cdefBF548.h"
#endif
#ifdef CONFIG_BF549
#include "cdefBF549.h"
#endif

#endif

#define BFIN_UART_NR_PORTS	4

#define OFFSET_DLL              0x00	/* Divisor Latch (Low-Byte)             */
#define OFFSET_DLH              0x04	/* Divisor Latch (High-Byte)            */
#define OFFSET_GCTL             0x08	/* Global Control Register              */
#define OFFSET_LCR              0x0C	/* Line Control Register                */
#define OFFSET_MCR              0x10	/* Modem Control Register               */
#define OFFSET_LSR              0x14	/* Line Status Register                 */
#define OFFSET_MSR              0x18	/* Modem Status Register                */
#define OFFSET_SCR              0x1C	/* SCR Scratch Register                 */
#define OFFSET_IER_SET          0x20	/* Set Interrupt Enable Register        */
#define OFFSET_IER_CLEAR        0x24	/* Clear Interrupt Enable Register      */
#define OFFSET_THR              0x28	/* Transmit Holding register            */
#define OFFSET_RBR              0x2C	/* Receive Buffer register              */

/* PLL_DIV Masks */
#define CCLK_DIV1 CSEL_DIV1	/* CCLK = VCO / 1 */
#define CCLK_DIV2 CSEL_DIV2	/* CCLK = VCO / 2 */
#define CCLK_DIV4 CSEL_DIV4	/* CCLK = VCO / 4 */
#define CCLK_DIV8 CSEL_DIV8	/* CCLK = VCO / 8 */

#endif
