/*
 * File:         include/asm-blackfin/mach-bf537/blackfin.h
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

#define BF537_FAMILY

#include "bf537.h"
#include "mem_map.h"
#include "defBF534.h"
#include "anomaly.h"

#if defined(CONFIG_BF537) || defined(CONFIG_BF536)
#include "defBF537.h"
#endif

#if !defined(__ASSEMBLY__)
#include "cdefBF534.h"

/* UART 0*/
#define bfin_read_UART_THR() bfin_read_UART0_THR()
#define bfin_write_UART_THR(val) bfin_write_UART0_THR(val)
#define bfin_read_UART_RBR() bfin_read_UART0_RBR()
#define bfin_write_UART_RBR(val) bfin_write_UART0_RBR(val)
#define bfin_read_UART_DLL() bfin_read_UART0_DLL()
#define bfin_write_UART_DLL(val) bfin_write_UART0_DLL(val)
#define bfin_read_UART_IER() bfin_read_UART0_IER()
#define bfin_write_UART_IER(val) bfin_write_UART0_IER(val)
#define bfin_read_UART_DLH() bfin_read_UART0_DLH()
#define bfin_write_UART_DLH(val) bfin_write_UART0_DLH(val)
#define bfin_read_UART_IIR() bfin_read_UART0_IIR()
#define bfin_write_UART_IIR(val) bfin_write_UART0_IIR(val)
#define bfin_read_UART_LCR() bfin_read_UART0_LCR()
#define bfin_write_UART_LCR(val) bfin_write_UART0_LCR(val)
#define bfin_read_UART_MCR() bfin_read_UART0_MCR()
#define bfin_write_UART_MCR(val) bfin_write_UART0_MCR(val)
#define bfin_read_UART_LSR() bfin_read_UART0_LSR()
#define bfin_write_UART_LSR(val) bfin_write_UART0_LSR(val)
#define bfin_read_UART_SCR() bfin_read_UART0_SCR()
#define bfin_write_UART_SCR(val) bfin_write_UART0_SCR(val)
#define bfin_read_UART_GCTL() bfin_read_UART0_GCTL()
#define bfin_write_UART_GCTL(val) bfin_write_UART0_GCTL(val)

#if defined(CONFIG_BF537) || defined(CONFIG_BF536)
#include "cdefBF537.h"
#endif
#endif

/* MAP used DEFINES from BF533 to BF537 - so we don't need to change them in the driver, kernel, etc. */

/* UART_IIR Register */
#define STATUS(x)	((x << 1) & 0x06)
#define STATUS_P1	0x02
#define STATUS_P0	0x01

/* DMA Channel */
#define bfin_read_CH_UART_RX() bfin_read_CH_UART0_RX()
#define bfin_write_CH_UART_RX(val) bfin_write_CH_UART0_RX(val)
#define CH_UART_RX CH_UART0_RX
#define bfin_read_CH_UART_TX() bfin_read_CH_UART0_TX()
#define bfin_write_CH_UART_TX(val) bfin_write_CH_UART0_TX(val)
#define CH_UART_TX CH_UART0_TX

/* System Interrupt Controller */
#define bfin_read_IRQ_UART_RX() bfin_read_IRQ_UART0_RX()
#define bfin_write_IRQ_UART_RX(val) bfin_write_IRQ_UART0_RX(val)
#define IRQ_UART_RX IRQ_UART0_RX
#define bfin_read_IRQ_UART_TX() bfin_read_IRQ_UART0_TX()
#define bfin_write_IRQ_UART_TX(val) bfin_write_IRQ_UART0_TX(val)
#define	IRQ_UART_TX IRQ_UART0_TX
#define bfin_read_IRQ_UART_ERROR() bfin_read_IRQ_UART0_ERROR()
#define bfin_write_IRQ_UART_ERROR(val) bfin_write_IRQ_UART0_ERROR(val)
#define	IRQ_UART_ERROR IRQ_UART0_ERROR

/* MMR Registers*/
#define bfin_read_UART_THR() bfin_read_UART0_THR()
#define bfin_write_UART_THR(val) bfin_write_UART0_THR(val)
#define BFIN_UART_THR UART0_THR
#define bfin_read_UART_RBR() bfin_read_UART0_RBR()
#define bfin_write_UART_RBR(val) bfin_write_UART0_RBR(val)
#define BFIN_UART_RBR UART0_RBR
#define bfin_read_UART_DLL() bfin_read_UART0_DLL()
#define bfin_write_UART_DLL(val) bfin_write_UART0_DLL(val)
#define BFIN_UART_DLL UART0_DLL
#define bfin_read_UART_IER() bfin_read_UART0_IER()
#define bfin_write_UART_IER(val) bfin_write_UART0_IER(val)
#define BFIN_UART_IER UART0_IER
#define bfin_read_UART_DLH() bfin_read_UART0_DLH()
#define bfin_write_UART_DLH(val) bfin_write_UART0_DLH(val)
#define BFIN_UART_DLH UART0_DLH
#define bfin_read_UART_IIR() bfin_read_UART0_IIR()
#define bfin_write_UART_IIR(val) bfin_write_UART0_IIR(val)
#define BFIN_UART_IIR UART0_IIR
#define bfin_read_UART_LCR() bfin_read_UART0_LCR()
#define bfin_write_UART_LCR(val) bfin_write_UART0_LCR(val)
#define BFIN_UART_LCR UART0_LCR
#define bfin_read_UART_MCR() bfin_read_UART0_MCR()
#define bfin_write_UART_MCR(val) bfin_write_UART0_MCR(val)
#define BFIN_UART_MCR UART0_MCR
#define bfin_read_UART_LSR() bfin_read_UART0_LSR()
#define bfin_write_UART_LSR(val) bfin_write_UART0_LSR(val)
#define BFIN_UART_LSR UART0_LSR
#define bfin_read_UART_SCR() bfin_read_UART0_SCR()
#define bfin_write_UART_SCR(val) bfin_write_UART0_SCR(val)
#define BFIN_UART_SCR  UART0_SCR
#define bfin_read_UART_GCTL() bfin_read_UART0_GCTL()
#define bfin_write_UART_GCTL(val) bfin_write_UART0_GCTL(val)
#define BFIN_UART_GCTL UART0_GCTL

#define BFIN_UART_NR_PORTS	2

#define OFFSET_THR              0x00	/* Transmit Holding register            */
#define OFFSET_RBR              0x00	/* Receive Buffer register              */
#define OFFSET_DLL              0x00	/* Divisor Latch (Low-Byte)             */
#define OFFSET_IER              0x04	/* Interrupt Enable Register            */
#define OFFSET_DLH              0x04	/* Divisor Latch (High-Byte)            */
#define OFFSET_IIR              0x08	/* Interrupt Identification Register    */
#define OFFSET_LCR              0x0C	/* Line Control Register                */
#define OFFSET_MCR              0x10	/* Modem Control Register               */
#define OFFSET_LSR              0x14	/* Line Status Register                 */
#define OFFSET_MSR              0x18	/* Modem Status Register                */
#define OFFSET_SCR              0x1C	/* SCR Scratch Register                 */
#define OFFSET_GCTL             0x24	/* Global Control Register              */

/* DPMC*/
#define bfin_read_STOPCK_OFF() bfin_read_STOPCK()
#define bfin_write_STOPCK_OFF(val) bfin_write_STOPCK(val)
#define STOPCK_OFF STOPCK

/* PLL_DIV Masks													*/
#define CCLK_DIV1 CSEL_DIV1	/*          CCLK = VCO / 1                                  */
#define CCLK_DIV2 CSEL_DIV2	/*          CCLK = VCO / 2                                  */
#define CCLK_DIV4 CSEL_DIV4	/*          CCLK = VCO / 4                                  */
#define CCLK_DIV8 CSEL_DIV8	/*          CCLK = VCO / 8                                  */

#endif
