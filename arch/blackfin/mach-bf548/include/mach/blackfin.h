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
#include "mem_map.h"
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

/* UART 1*/
#define bfin_read_UART_THR()		bfin_read_UART1_THR()
#define bfin_write_UART_THR(val)	bfin_write_UART1_THR(val)
#define bfin_read_UART_RBR()		bfin_read_UART1_RBR()
#define bfin_write_UART_RBR(val)	bfin_write_UART1_RBR(val)
#define bfin_read_UART_DLL()		bfin_read_UART1_DLL()
#define bfin_write_UART_DLL(val)	bfin_write_UART1_DLL(val)
#define bfin_read_UART_IER()		bfin_read_UART1_IER()
#define bfin_write_UART_IER(val)	bfin_write_UART1_IER(val)
#define bfin_read_UART_DLH()		bfin_read_UART1_DLH()
#define bfin_write_UART_DLH(val)	bfin_write_UART1_DLH(val)
#define bfin_read_UART_IIR()		bfin_read_UART1_IIR()
#define bfin_write_UART_IIR(val)	bfin_write_UART1_IIR(val)
#define bfin_read_UART_LCR()		bfin_read_UART1_LCR()
#define bfin_write_UART_LCR(val)	bfin_write_UART1_LCR(val)
#define bfin_read_UART_MCR()		bfin_read_UART1_MCR()
#define bfin_write_UART_MCR(val)	bfin_write_UART1_MCR(val)
#define bfin_read_UART_LSR()		bfin_read_UART1_LSR()
#define bfin_write_UART_LSR(val)	bfin_write_UART1_LSR(val)
#define bfin_read_UART_SCR()		bfin_read_UART1_SCR()
#define bfin_write_UART_SCR(val)	bfin_write_UART1_SCR(val)
#define bfin_read_UART_GCTL()		bfin_read_UART1_GCTL()
#define bfin_write_UART_GCTL(val)	bfin_write_UART1_GCTL(val)

#endif

/* MAP used DEFINES from BF533 to BF54x - so we don't need to change 
 * them in the driver, kernel, etc. */

/* UART_IIR Register */
#define STATUS(x)	((x << 1) & 0x06)
#define STATUS_P1	0x02
#define STATUS_P0	0x01

/* UART 0*/

/* DMA Channel */
#define bfin_read_CH_UART_RX()		bfin_read_CH_UART1_RX()
#define bfin_write_CH_UART_RX(val)	bfin_write_CH_UART1_RX(val)
#define bfin_read_CH_UART_TX()		bfin_read_CH_UART1_TX()
#define bfin_write_CH_UART_TX(val)	bfin_write_CH_UART1_TX(val)
#define CH_UART_RX			CH_UART1_RX
#define CH_UART_TX			CH_UART1_TX

/* System Interrupt Controller */
#define bfin_read_IRQ_UART_RX()		bfin_read_IRQ_UART1_RX()
#define bfin_write_IRQ_UART_RX(val)	bfin_write_IRQ_UART1_RX(val)
#define bfin_read_IRQ_UART_TX()		bfin_read_IRQ_UART1_TX()
#define bfin_write_IRQ_UART_TX(val)	bfin_write_IRQ_UART1_TX(val)
#define bfin_read_IRQ_UART_ERROR()	bfin_read_IRQ_UART1_ERROR()
#define bfin_write_IRQ_UART_ERROR(val)	bfin_write_IRQ_UART1_ERROR(val)
#define IRQ_UART_RX			IRQ_UART1_RX
#define	IRQ_UART_TX			IRQ_UART1_TX
#define	IRQ_UART_ERROR			IRQ_UART1_ERROR

/* MMR Registers*/
#define bfin_read_UART_THR()		bfin_read_UART1_THR()
#define bfin_write_UART_THR(val)	bfin_write_UART1_THR(val)
#define bfin_read_UART_RBR()		bfin_read_UART1_RBR()
#define bfin_write_UART_RBR(val)	bfin_write_UART1_RBR(val)
#define bfin_read_UART_DLL()		bfin_read_UART1_DLL()
#define bfin_write_UART_DLL(val)	bfin_write_UART1_DLL(val)
#define bfin_read_UART_IER()		bfin_read_UART1_IER()
#define bfin_write_UART_IER(val)	bfin_write_UART1_IER(val)
#define bfin_read_UART_DLH()		bfin_read_UART1_DLH()
#define bfin_write_UART_DLH(val)	bfin_write_UART1_DLH(val)
#define bfin_read_UART_IIR()		bfin_read_UART1_IIR()
#define bfin_write_UART_IIR(val)	bfin_write_UART1_IIR(val)
#define bfin_read_UART_LCR()		bfin_read_UART1_LCR()
#define bfin_write_UART_LCR(val)	bfin_write_UART1_LCR(val)
#define bfin_read_UART_MCR()		bfin_read_UART1_MCR()
#define bfin_write_UART_MCR(val)	bfin_write_UART1_MCR(val)
#define bfin_read_UART_LSR()		bfin_read_UART1_LSR()
#define bfin_write_UART_LSR(val)	bfin_write_UART1_LSR(val)
#define bfin_read_UART_SCR()		bfin_read_UART1_SCR()
#define bfin_write_UART_SCR(val)	bfin_write_UART1_SCR(val)
#define bfin_read_UART_GCTL()		bfin_read_UART1_GCTL()
#define bfin_write_UART_GCTL(val)	bfin_write_UART1_GCTL(val)

#define BFIN_UART_THR			UART1_THR
#define BFIN_UART_RBR			UART1_RBR
#define BFIN_UART_DLL			UART1_DLL
#define BFIN_UART_IER			UART1_IER
#define BFIN_UART_DLH			UART1_DLH
#define BFIN_UART_IIR			UART1_IIR
#define BFIN_UART_LCR			UART1_LCR
#define BFIN_UART_MCR			UART1_MCR
#define BFIN_UART_LSR			UART1_LSR
#define BFIN_UART_SCR			UART1_SCR
#define BFIN_UART_GCTL			UART1_GCTL

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
