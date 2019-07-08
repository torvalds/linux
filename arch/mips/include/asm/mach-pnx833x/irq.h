/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  irq.h: IRQ mappings for PNX833X.
 *
 *  Copyright 2008 NXP Semiconductors
 *	  Chris Steel <chris.steel@nxp.com>
 *    Daniel Laird <daniel.j.laird@nxp.com>
 */

#ifndef __ASM_MIPS_MACH_PNX833X_IRQ_H
#define __ASM_MIPS_MACH_PNX833X_IRQ_H
/*
 * The "IRQ numbers" are completely virtual.
 *
 * In PNX8330/1, we have 48 interrupt lines, numbered from 1 to 48.
 * Let's use numbers 1..48 for PIC interrupts, number 0 for timer interrupt,
 * numbers 49..64 for (virtual) GPIO interrupts.
 *
 * In PNX8335, we have 57 interrupt lines, numbered from 1 to 57,
 * connected to PIC, which uses core hardware interrupt 2, and also
 * a timer interrupt through hardware interrupt 5.
 * Let's use numbers 1..64 for PIC interrupts, number 0 for timer interrupt,
 * numbers 65..80 for (virtual) GPIO interrupts.
 *
 */
#if defined(CONFIG_SOC_PNX8335)
	#define PNX833X_PIC_NUM_IRQ			58
#else
	#define PNX833X_PIC_NUM_IRQ			37
#endif

#define MIPS_CPU_NUM_IRQ				8
#define PNX833X_GPIO_NUM_IRQ			16

#define MIPS_CPU_IRQ_BASE				0
#define PNX833X_PIC_IRQ_BASE			(MIPS_CPU_IRQ_BASE + MIPS_CPU_NUM_IRQ)
#define PNX833X_GPIO_IRQ_BASE			(PNX833X_PIC_IRQ_BASE + PNX833X_PIC_NUM_IRQ)
#define NR_IRQS							(MIPS_CPU_NUM_IRQ + PNX833X_PIC_NUM_IRQ + PNX833X_GPIO_NUM_IRQ)

#endif
