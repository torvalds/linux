/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Defines for the MSP interrupt controller.
 *
 * Copyright (C) 1999 MIPS Technologies, Inc.  All rights reserved.
 * Author: Carsten Langgaard, carstenl@mips.com
 *
 * ########################################################################
 *
 * ########################################################################
 */

#ifndef _MSP_CIC_INT_H
#define _MSP_CIC_INT_H

/*
 * The PMC-Sierra CIC interrupts are all centrally managed by the
 * CIC sub-system.
 * We attempt to keep the interrupt numbers as consistent as possible
 * across all of the MSP devices, but some differences will creep in ...
 * The interrupts which are directly forwarded to the MIPS core interrupts
 * are assigned interrupts in the range 0-7, interrupts cascaded through
 * the CIC are assigned interrupts 8-39.  The cascade occurs on C_IRQ4
 * (MSP_INT_CIC).  Currently we don't really distinguish between VPE1
 * and VPE0 (or thread contexts for that matter).  Will have to fix.
 * The PER interrupts are assigned interrupts in the range 40-71.
*/


/*
 * IRQs directly forwarded to the CPU
 */
#define MSP_MIPS_INTBASE	0
#define MSP_INT_SW0		0	/* IRQ for swint0,	 C_SW0	*/
#define MSP_INT_SW1		1	/* IRQ for swint1,	 C_SW1	*/
#define MSP_INT_MAC0		2	/* IRQ for MAC 0,	 C_IRQ0 */
#define MSP_INT_MAC1		3	/* IRQ for MAC 1,	 C_IRQ1 */
#define MSP_INT_USB		4	/* IRQ for USB,		 C_IRQ2 */
#define MSP_INT_SAR		5	/* IRQ for ADSL2+ SAR,	 C_IRQ3 */
#define MSP_INT_CIC		6	/* IRQ for CIC block,	 C_IRQ4 */
#define MSP_INT_SEC		7	/* IRQ for Sec engine,	 C_IRQ5 */

/*
 * IRQs cascaded on CPU interrupt 4 (CAUSE bit 12, C_IRQ4)
 * These defines should be tied to the register definitions for the CIC
 * interrupt routine.  For now, just use hard-coded values.
 */
#define MSP_CIC_INTBASE		(MSP_MIPS_INTBASE + 8)
#define MSP_INT_EXT0		(MSP_CIC_INTBASE + 0)
					/* External interrupt 0		*/
#define MSP_INT_EXT1		(MSP_CIC_INTBASE + 1)
					/* External interrupt 1		*/
#define MSP_INT_EXT2		(MSP_CIC_INTBASE + 2)
					/* External interrupt 2		*/
#define MSP_INT_EXT3		(MSP_CIC_INTBASE + 3)
					/* External interrupt 3		*/
#define MSP_INT_CPUIF		(MSP_CIC_INTBASE + 4)
					/* CPU interface interrupt	*/
#define MSP_INT_EXT4		(MSP_CIC_INTBASE + 5)
					/* External interrupt 4		*/
#define MSP_INT_CIC_USB		(MSP_CIC_INTBASE + 6)
					/* Cascaded IRQ for USB		*/
#define MSP_INT_MBOX		(MSP_CIC_INTBASE + 7)
					/* Sec engine mailbox IRQ	*/
#define MSP_INT_EXT5		(MSP_CIC_INTBASE + 8)
					/* External interrupt 5		*/
#define MSP_INT_TDM		(MSP_CIC_INTBASE + 9)
					/* TDM interrupt		*/
#define MSP_INT_CIC_MAC0	(MSP_CIC_INTBASE + 10)
					/* Cascaded IRQ for MAC 0	*/
#define MSP_INT_CIC_MAC1	(MSP_CIC_INTBASE + 11)
					/* Cascaded IRQ for MAC 1	*/
#define MSP_INT_CIC_SEC		(MSP_CIC_INTBASE + 12)
					/* Cascaded IRQ for sec engine	*/
#define MSP_INT_PER		(MSP_CIC_INTBASE + 13)
					/* Peripheral interrupt		*/
#define MSP_INT_TIMER0		(MSP_CIC_INTBASE + 14)
					/* SLP timer 0			*/
#define MSP_INT_TIMER1		(MSP_CIC_INTBASE + 15)
					/* SLP timer 1			*/
#define MSP_INT_TIMER2		(MSP_CIC_INTBASE + 16)
					/* SLP timer 2			*/
#define MSP_INT_VPE0_TIMER	(MSP_CIC_INTBASE + 17)
					/* VPE0 MIPS timer		*/
#define MSP_INT_BLKCP		(MSP_CIC_INTBASE + 18)
					/* Block Copy			*/
#define MSP_INT_UART0		(MSP_CIC_INTBASE + 19)
					/* UART 0			*/
#define MSP_INT_PCI		(MSP_CIC_INTBASE + 20)
					/* PCI subsystem		*/
#define MSP_INT_EXT6		(MSP_CIC_INTBASE + 21)
					/* External interrupt 5		*/
#define MSP_INT_PCI_MSI		(MSP_CIC_INTBASE + 22)
					/* PCI Message Signal		*/
#define MSP_INT_CIC_SAR		(MSP_CIC_INTBASE + 23)
					/* Cascaded ADSL2+ SAR IRQ	*/
#define MSP_INT_DSL		(MSP_CIC_INTBASE + 24)
					/* ADSL2+ IRQ			*/
#define MSP_INT_CIC_ERR		(MSP_CIC_INTBASE + 25)
					/* SLP error condition		*/
#define MSP_INT_VPE1_TIMER	(MSP_CIC_INTBASE + 26)
					/* VPE1 MIPS timer		*/
#define MSP_INT_VPE0_PC		(MSP_CIC_INTBASE + 27)
					/* VPE0 Performance counter	*/
#define MSP_INT_VPE1_PC		(MSP_CIC_INTBASE + 28)
					/* VPE1 Performance counter	*/
#define MSP_INT_EXT7		(MSP_CIC_INTBASE + 29)
					/* External interrupt 5		*/
#define MSP_INT_VPE0_SW		(MSP_CIC_INTBASE + 30)
					/* VPE0 Software interrupt	*/
#define MSP_INT_VPE1_SW		(MSP_CIC_INTBASE + 31)
					/* VPE0 Software interrupt	*/

/*
 * IRQs cascaded on CIC PER interrupt (MSP_INT_PER)
 */
#define MSP_PER_INTBASE		(MSP_CIC_INTBASE + 32)
/* Reserved					   0-1			*/
#define MSP_INT_UART1		(MSP_PER_INTBASE + 2)
					/* UART 1			*/
/* Reserved					   3-5			*/
#define MSP_INT_2WIRE		(MSP_PER_INTBASE + 6)
					/* 2-wire			*/
#define MSP_INT_TM0		(MSP_PER_INTBASE + 7)
					/* Peripheral timer block out 0 */
#define MSP_INT_TM1		(MSP_PER_INTBASE + 8)
					/* Peripheral timer block out 1 */
/* Reserved					   9			*/
#define MSP_INT_SPRX		(MSP_PER_INTBASE + 10)
					/* SPI RX complete		*/
#define MSP_INT_SPTX		(MSP_PER_INTBASE + 11)
					/* SPI TX complete		*/
#define MSP_INT_GPIO		(MSP_PER_INTBASE + 12)
					/* GPIO				*/
#define MSP_INT_PER_ERR		(MSP_PER_INTBASE + 13)
					/* Peripheral error		*/
/* Reserved					   14-31		*/

#endif /* !_MSP_CIC_INT_H */
