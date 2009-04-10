/* MN10300 On-board interrupt controller registers
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_INTCTL_REGS_H
#define _ASM_INTCTL_REGS_H

#include <asm/cpu-regs.h>

#ifdef __KERNEL__

/* interrupt controller registers */
#define GxICR(X)		__SYSREG(0xd4000000 + (X) * 4, u16)	/* group irq ctrl regs */

#define IAGR			__SYSREG(0xd4000100, u16)	/* intr acceptance group reg */
#define IAGR_GN			0x00fc		/* group number register
						 * (documentation _has_ to be wrong)
						 */

#define EXTMD			__SYSREG(0xd4000200, u16)	/* external pin intr spec reg */
#define GET_XIRQ_TRIGGER(X) ((EXTMD >> ((X) * 2)) & 3)

#define SET_XIRQ_TRIGGER(X,Y)			\
do {						\
	u16 x = EXTMD;				\
	x &= ~(3 << ((X) * 2));			\
	x |= ((Y) & 3) << ((X) * 2);		\
	EXTMD = x;				\
} while (0)

#define XIRQ_TRIGGER_LOWLEVEL	0
#define XIRQ_TRIGGER_HILEVEL	1
#define XIRQ_TRIGGER_NEGEDGE	2
#define XIRQ_TRIGGER_POSEDGE	3

/* non-maskable interrupt control */
#define NMIIRQ			0
#define NMICR			GxICR(NMIIRQ)	/* NMI control register */
#define NMICR_NMIF		0x0001		/* NMI pin interrupt flag */
#define NMICR_WDIF		0x0002		/* watchdog timer overflow flag */
#define NMICR_ABUSERR		0x0008		/* async bus error flag */

/* maskable interrupt control */
#define GxICR_DETECT		0x0001		/* interrupt detect flag */
#define GxICR_REQUEST		0x0010		/* interrupt request flag */
#define GxICR_ENABLE		0x0100		/* interrupt enable flag */
#define GxICR_LEVEL		0x7000		/* interrupt priority level */
#define GxICR_LEVEL_0		0x0000		/* - level 0 */
#define GxICR_LEVEL_1		0x1000		/* - level 1 */
#define GxICR_LEVEL_2		0x2000		/* - level 2 */
#define GxICR_LEVEL_3		0x3000		/* - level 3 */
#define GxICR_LEVEL_4		0x4000		/* - level 4 */
#define GxICR_LEVEL_5		0x5000		/* - level 5 */
#define GxICR_LEVEL_6		0x6000		/* - level 6 */
#define GxICR_LEVEL_SHIFT	12

#ifndef __ASSEMBLY__
extern void set_intr_level(int irq, u16 level);
extern void set_intr_postackable(int irq);
#endif

/* external interrupts */
#define XIRQxICR(X)		GxICR((X))	/* external interrupt control regs */

#endif /* __KERNEL__ */

#endif /* _ASM_INTCTL_REGS_H */
