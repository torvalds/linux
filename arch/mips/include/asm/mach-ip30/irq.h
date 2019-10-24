/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HEART IRQ defines
 *
 * Copyright (C) 2009 Johannes Dickgreber <tanzy@gmx.de>
 *		 2014-2016 Joshua Kinard <kumba@gentoo.org>
 *
 */

#ifndef __ASM_MACH_IP30_IRQ_H
#define __ASM_MACH_IP30_IRQ_H

/*
 * HEART has 64 hardware interrupts, but use 128 to leave room for a few
 * software interrupts as well (such as the CPU timer interrupt.
 */
#define NR_IRQS				128

extern void __init ip30_install_ipi(void);

/*
 * HEART has 64 interrupt vectors available to it, subdivided into five
 * priority levels.  They are numbered 0 to 63.
 */
#define HEART_NUM_IRQS			64

/*
 * These are the five interrupt priority levels and their corresponding
 * CPU IPx interrupt pins.
 *
 * Level 4 - Error Interrupts.
 * Level 3 - HEART timer interrupt.
 * Level 2 - CPU IPI, CPU debug, power putton, general device interrupts.
 * Level 1 - General device interrupts.
 * Level 0 - General device GFX flow control interrupts.
 */
#define HEART_L4_INT_MASK		0xfff8000000000000ULL	/* IP6 */
#define HEART_L3_INT_MASK		0x0004000000000000ULL	/* IP5 */
#define HEART_L2_INT_MASK		0x0003ffff00000000ULL	/* IP4 */
#define HEART_L1_INT_MASK		0x00000000ffff0000ULL	/* IP3 */
#define HEART_L0_INT_MASK		0x000000000000ffffULL	/* IP2 */

/* HEART L0 Interrupts (Low Priority) */
#define HEART_L0_INT_GENERIC		 0
#define HEART_L0_INT_FLOW_CTRL_HWTR_0	 1
#define HEART_L0_INT_FLOW_CTRL_HWTR_1	 2

/* HEART L2 Interrupts (High Priority) */
#define HEART_L2_INT_RESCHED_CPU_0	46
#define HEART_L2_INT_RESCHED_CPU_1	47
#define HEART_L2_INT_CALL_CPU_0		48
#define HEART_L2_INT_CALL_CPU_1		49

/* HEART L3 Interrupts (Compare/Counter Timer) */
#define HEART_L3_INT_TIMER		50

/* HEART L4 Interrupts (Errors) */
#define HEART_L4_INT_XWID_ERR_9		51
#define HEART_L4_INT_XWID_ERR_A		52
#define HEART_L4_INT_XWID_ERR_B		53
#define HEART_L4_INT_XWID_ERR_C		54
#define HEART_L4_INT_XWID_ERR_D		55
#define HEART_L4_INT_XWID_ERR_E		56
#define HEART_L4_INT_XWID_ERR_F		57
#define HEART_L4_INT_XWID_ERR_XBOW	58
#define HEART_L4_INT_CPU_BUS_ERR_0	59
#define HEART_L4_INT_CPU_BUS_ERR_1	60
#define HEART_L4_INT_CPU_BUS_ERR_2	61
#define HEART_L4_INT_CPU_BUS_ERR_3	62
#define HEART_L4_INT_HEART_EXCP		63

/*
 * Power Switch is wired via BaseIO BRIDGE slot #6.
 *
 * ACFail is wired via BaseIO BRIDGE slot #7.
 */
#define IP30_POWER_IRQ		HEART_L2_INT_POWER_BTN

#include_next <irq.h>

#define IP30_HEART_L0_IRQ	(MIPS_CPU_IRQ_BASE + 2)
#define IP30_HEART_L1_IRQ	(MIPS_CPU_IRQ_BASE + 3)
#define IP30_HEART_L2_IRQ	(MIPS_CPU_IRQ_BASE + 4)
#define IP30_HEART_TIMER_IRQ	(MIPS_CPU_IRQ_BASE + 5)
#define IP30_HEART_ERR_IRQ	(MIPS_CPU_IRQ_BASE + 6)

#endif /* __ASM_MACH_IP30_IRQ_H */
