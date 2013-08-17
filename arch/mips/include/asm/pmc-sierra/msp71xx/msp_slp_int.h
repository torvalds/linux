/*
 * Defines for the MSP interrupt controller.
 *
 * Copyright (C) 1999 MIPS Technologies, Inc.  All rights reserved.
 * Author: Carsten Langgaard, carstenl@mips.com
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 */

#ifndef _MSP_SLP_INT_H
#define _MSP_SLP_INT_H

/*
 * The PMC-Sierra SLP interrupts are arranged in a 3 level cascaded
 * hierarchical system.  The first level are the direct MIPS interrupts
 * and are assigned the interrupt range 0-7.  The second level is the SLM
 * interrupt controller and is assigned the range 8-39.  The third level
 * comprises the Peripherial block, the PCI block, the PCI MSI block and
 * the SLP.  The PCI interrupts and the SLP errors are handled by the
 * relevant subsystems so the core interrupt code needs only concern
 * itself with the Peripheral block.  These are assigned interrupts in
 * the range 40-71.
 */

/*
 * IRQs directly connected to CPU
 */
#define MSP_MIPS_INTBASE	0
#define MSP_INT_SW0		0  /* IRQ for swint0,         C_SW0  */
#define MSP_INT_SW1		1  /* IRQ for swint1,         C_SW1  */
#define MSP_INT_MAC0 		2  /* IRQ for MAC 0,          C_IRQ0 */
#define MSP_INT_MAC1		3  /* IRQ for MAC 1,          C_IRQ1 */
#define MSP_INT_C_IRQ2		4  /* Wired off,              C_IRQ2 */
#define MSP_INT_VE		5  /* IRQ for Voice Engine,   C_IRQ3 */
#define MSP_INT_SLP		6  /* IRQ for SLM block,      C_IRQ4 */
#define MSP_INT_TIMER		7  /* IRQ for the MIPS timer, C_IRQ5 */

/*
 * IRQs cascaded on CPU interrupt 4 (CAUSE bit 12, C_IRQ4)
 * These defines should be tied to the register definition for the SLM
 * interrupt routine.  For now, just use hard-coded values.
 */
#define MSP_SLP_INTBASE		(MSP_MIPS_INTBASE + 8)
#define MSP_INT_EXT0		(MSP_SLP_INTBASE + 0)
					/* External interrupt 0         */
#define MSP_INT_EXT1		(MSP_SLP_INTBASE + 1)
					/* External interrupt 1         */
#define MSP_INT_EXT2		(MSP_SLP_INTBASE + 2)
					/* External interrupt 2         */
#define MSP_INT_EXT3		(MSP_SLP_INTBASE + 3)
					/* External interrupt 3         */
/* Reserved					   4-7                  */

/*
 *************************************************************************
 * DANGER/DANGER/DANGER/DANGER/DANGER/DANGER/DANGER/DANGER/DANGER/DANGER *
 * Some MSP produces have this interrupt labelled as Voice and some are  *
 * SEC mbox ...                                                          *
 *************************************************************************
 */
#define MSP_INT_SLP_VE		(MSP_SLP_INTBASE + 8)
					/* Cascaded IRQ for Voice Engine*/
#define MSP_INT_SLP_TDM		(MSP_SLP_INTBASE + 9)
					/* TDM interrupt                */
#define MSP_INT_SLP_MAC0	(MSP_SLP_INTBASE + 10)
					/* Cascaded IRQ for MAC 0       */
#define MSP_INT_SLP_MAC1	(MSP_SLP_INTBASE + 11)
					/* Cascaded IRQ for MAC 1       */
#define MSP_INT_SEC		(MSP_SLP_INTBASE + 12)
					/* IRQ for security engine      */
#define	MSP_INT_PER		(MSP_SLP_INTBASE + 13)
					/* Peripheral interrupt         */
#define	MSP_INT_TIMER0		(MSP_SLP_INTBASE + 14)
					/* SLP timer 0                  */
#define	MSP_INT_TIMER1		(MSP_SLP_INTBASE + 15)
					/* SLP timer 1                  */
#define	MSP_INT_TIMER2		(MSP_SLP_INTBASE + 16)
					/* SLP timer 2                  */
#define	MSP_INT_SLP_TIMER	(MSP_SLP_INTBASE + 17)
					/* Cascaded MIPS timer          */
#define MSP_INT_BLKCP		(MSP_SLP_INTBASE + 18)
					/* Block Copy                   */
#define MSP_INT_UART0		(MSP_SLP_INTBASE + 19)
					/* UART 0                       */
#define MSP_INT_PCI		(MSP_SLP_INTBASE + 20)
					/* PCI subsystem                */
#define MSP_INT_PCI_DBELL	(MSP_SLP_INTBASE + 21)
					/* PCI doorbell                 */
#define MSP_INT_PCI_MSI		(MSP_SLP_INTBASE + 22)
					/* PCI Message Signal           */
#define MSP_INT_PCI_BC0		(MSP_SLP_INTBASE + 23)
					/* PCI Block Copy 0             */
#define MSP_INT_PCI_BC1		(MSP_SLP_INTBASE + 24)
					/* PCI Block Copy 1             */
#define MSP_INT_SLP_ERR		(MSP_SLP_INTBASE + 25)
					/* SLP error condition          */
#define MSP_INT_MAC2		(MSP_SLP_INTBASE + 26)
					/* IRQ for MAC2                 */
/* Reserved					   26-31                */

/*
 * IRQs cascaded on SLP PER interrupt (MSP_INT_PER)
 */
#define MSP_PER_INTBASE		(MSP_SLP_INTBASE + 32)
/* Reserved					   0-1                  */
#define MSP_INT_UART1		(MSP_PER_INTBASE + 2)
					/* UART 1                       */
/* Reserved					   3-5                  */
#define MSP_INT_2WIRE		(MSP_PER_INTBASE + 6)
					/* 2-wire                       */
#define MSP_INT_TM0		(MSP_PER_INTBASE + 7)
					/* Peripheral timer block out 0 */
#define MSP_INT_TM1		(MSP_PER_INTBASE + 8)
					/* Peripheral timer block out 1 */
/* Reserved					   9                    */
#define MSP_INT_SPRX		(MSP_PER_INTBASE + 10)
					/* SPI RX complete              */
#define MSP_INT_SPTX		(MSP_PER_INTBASE + 11)
					/* SPI TX complete              */
#define MSP_INT_GPIO		(MSP_PER_INTBASE + 12)
					/* GPIO                         */
#define MSP_INT_PER_ERR		(MSP_PER_INTBASE + 13)
					/* Peripheral error             */
/* Reserved					   14-31                */

#endif /* !_MSP_SLP_INT_H */
