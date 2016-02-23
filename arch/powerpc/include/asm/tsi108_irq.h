/*
 * (C) Copyright 2005 Tundra Semiconductor Corp.
 * Alex Bounine, <alexandreb at tundra.com).
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/*
 * definitions for interrupt controller initialization and external interrupt
 * demultiplexing on TSI108EMU/SVB boards.
 */

#ifndef _ASM_POWERPC_TSI108_IRQ_H
#define _ASM_POWERPC_TSI108_IRQ_H

/*
 * Tsi108 interrupts
 */
#ifndef TSI108_IRQ_REG_BASE
#define TSI108_IRQ_REG_BASE		0
#endif

#define TSI108_IRQ(x)		(TSI108_IRQ_REG_BASE + (x))

#define TSI108_MAX_VECTORS	(36 + 4)	/* 36 sources + PCI INT demux */
#define MAX_TASK_PRIO	0xF

#define TSI108_IRQ_SPURIOUS	(TSI108_MAX_VECTORS)

#define DEFAULT_PRIO_LVL	10	/* initial priority level */

/* Interrupt vectors assignment to external and internal
 * sources of requests. */

/* EXTERNAL INTERRUPT SOURCES */

#define IRQ_TSI108_EXT_INT0	TSI108_IRQ(0)	/* External Source at INT[0] */
#define IRQ_TSI108_EXT_INT1	TSI108_IRQ(1)	/* External Source at INT[1] */
#define IRQ_TSI108_EXT_INT2	TSI108_IRQ(2)	/* External Source at INT[2] */
#define IRQ_TSI108_EXT_INT3	TSI108_IRQ(3)	/* External Source at INT[3] */

/* INTERNAL INTERRUPT SOURCES */

#define IRQ_TSI108_RESERVED0	TSI108_IRQ(4)	/* Reserved IRQ */
#define IRQ_TSI108_RESERVED1	TSI108_IRQ(5)	/* Reserved IRQ */
#define IRQ_TSI108_RESERVED2	TSI108_IRQ(6)	/* Reserved IRQ */
#define IRQ_TSI108_RESERVED3	TSI108_IRQ(7)	/* Reserved IRQ */
#define IRQ_TSI108_DMA0		TSI108_IRQ(8)	/* DMA0 */
#define IRQ_TSI108_DMA1		TSI108_IRQ(9)	/* DMA1 */
#define IRQ_TSI108_DMA2		TSI108_IRQ(10)	/* DMA2 */
#define IRQ_TSI108_DMA3		TSI108_IRQ(11)	/* DMA3 */
#define IRQ_TSI108_UART0	TSI108_IRQ(12)	/* UART0 */
#define IRQ_TSI108_UART1	TSI108_IRQ(13)	/* UART1 */
#define IRQ_TSI108_I2C		TSI108_IRQ(14)	/* I2C */
#define IRQ_TSI108_GPIO		TSI108_IRQ(15)	/* GPIO */
#define IRQ_TSI108_GIGE0	TSI108_IRQ(16)	/* GIGE0 */
#define IRQ_TSI108_GIGE1	TSI108_IRQ(17)	/* GIGE1 */
#define IRQ_TSI108_RESERVED4	TSI108_IRQ(18)	/* Reserved IRQ */
#define IRQ_TSI108_HLP		TSI108_IRQ(19)	/* HLP */
#define IRQ_TSI108_SDRAM	TSI108_IRQ(20)	/* SDC */
#define IRQ_TSI108_PROC_IF	TSI108_IRQ(21)	/* Processor IF */
#define IRQ_TSI108_RESERVED5	TSI108_IRQ(22)	/* Reserved IRQ */
#define IRQ_TSI108_PCI		TSI108_IRQ(23)	/* PCI/X block */

#define IRQ_TSI108_MBOX0	TSI108_IRQ(24)	/* Mailbox 0 register */
#define IRQ_TSI108_MBOX1	TSI108_IRQ(25)	/* Mailbox 1 register */
#define IRQ_TSI108_MBOX2	TSI108_IRQ(26)	/* Mailbox 2 register */
#define IRQ_TSI108_MBOX3	TSI108_IRQ(27)	/* Mailbox 3 register */

#define IRQ_TSI108_DBELL0	TSI108_IRQ(28)	/* Doorbell 0 */
#define IRQ_TSI108_DBELL1	TSI108_IRQ(29)	/* Doorbell 1 */
#define IRQ_TSI108_DBELL2	TSI108_IRQ(30)	/* Doorbell 2 */
#define IRQ_TSI108_DBELL3	TSI108_IRQ(31)	/* Doorbell 3 */

#define IRQ_TSI108_TIMER0	TSI108_IRQ(32)	/* Global Timer 0 */
#define IRQ_TSI108_TIMER1	TSI108_IRQ(33)	/* Global Timer 1 */
#define IRQ_TSI108_TIMER2	TSI108_IRQ(34)	/* Global Timer 2 */
#define IRQ_TSI108_TIMER3	TSI108_IRQ(35)	/* Global Timer 3 */

/*
 * PCI bus INTA# - INTD# lines demultiplexor
 */
#define IRQ_PCI_INTAD_BASE	TSI108_IRQ(36)
#define IRQ_PCI_INTA		(IRQ_PCI_INTAD_BASE + 0)
#define IRQ_PCI_INTB		(IRQ_PCI_INTAD_BASE + 1)
#define IRQ_PCI_INTC		(IRQ_PCI_INTAD_BASE + 2)
#define IRQ_PCI_INTD		(IRQ_PCI_INTAD_BASE + 3)
#define NUM_PCI_IRQS		(4)

/* number of entries in vector dispatch table */
#define IRQ_TSI108_TAB_SIZE	(TSI108_MAX_VECTORS + 1)

/* Mapping of MPIC outputs to processors' interrupt pins */

#define IDIR_INT_OUT0		0x1
#define IDIR_INT_OUT1		0x2
#define IDIR_INT_OUT2		0x4
#define IDIR_INT_OUT3		0x8

/*---------------------------------------------------------------
 * IRQ line configuration parameters */

/* Interrupt delivery modes */
typedef enum {
	TSI108_IRQ_DIRECTED,
	TSI108_IRQ_DISTRIBUTED,
} TSI108_IRQ_MODE;
#endif				/*  _ASM_POWERPC_TSI108_IRQ_H */
