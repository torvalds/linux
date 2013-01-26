/*
 * arch/powerpc/sysdev/qe_lib/qe_ic.h
 *
 * QUICC ENGINE Interrupt Controller Header
 *
 * Copyright (C) 2006 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Li Yang <leoli@freescale.com>
 * Based on code from Shlomi Gridish <gridish@freescale.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef _POWERPC_SYSDEV_QE_IC_H
#define _POWERPC_SYSDEV_QE_IC_H

#include <asm/qe_ic.h>

#define NR_QE_IC_INTS		64

/* QE IC registers offset */
#define QEIC_CICR		0x00
#define QEIC_CIVEC		0x04
#define QEIC_CRIPNR		0x08
#define QEIC_CIPNR		0x0c
#define QEIC_CIPXCC		0x10
#define QEIC_CIPYCC		0x14
#define QEIC_CIPWCC		0x18
#define QEIC_CIPZCC		0x1c
#define QEIC_CIMR		0x20
#define QEIC_CRIMR		0x24
#define QEIC_CICNR		0x28
#define QEIC_CIPRTA		0x30
#define QEIC_CIPRTB		0x34
#define QEIC_CRICR		0x3c
#define QEIC_CHIVEC		0x60

/* Interrupt priority registers */
#define CIPCC_SHIFT_PRI0	29
#define CIPCC_SHIFT_PRI1	26
#define CIPCC_SHIFT_PRI2	23
#define CIPCC_SHIFT_PRI3	20
#define CIPCC_SHIFT_PRI4	13
#define CIPCC_SHIFT_PRI5	10
#define CIPCC_SHIFT_PRI6	7
#define CIPCC_SHIFT_PRI7	4

/* CICR priority modes */
#define CICR_GWCC		0x00040000
#define CICR_GXCC		0x00020000
#define CICR_GYCC		0x00010000
#define CICR_GZCC		0x00080000
#define CICR_GRTA		0x00200000
#define CICR_GRTB		0x00400000
#define CICR_HPIT_SHIFT		8
#define CICR_HPIT_MASK		0x00000300
#define CICR_HP_SHIFT		24
#define CICR_HP_MASK		0x3f000000

/* CICNR */
#define CICNR_WCC1T_SHIFT	20
#define CICNR_ZCC1T_SHIFT	28
#define CICNR_YCC1T_SHIFT	12
#define CICNR_XCC1T_SHIFT	4

/* CRICR */
#define CRICR_RTA1T_SHIFT	20
#define CRICR_RTB1T_SHIFT	28

/* Signal indicator */
#define SIGNAL_MASK		3
#define SIGNAL_HIGH		2
#define SIGNAL_LOW		0

struct qe_ic {
	/* Control registers offset */
	volatile u32 __iomem *regs;

	/* The remapper for this QEIC */
	struct irq_domain *irqhost;

	/* The "linux" controller struct */
	struct irq_chip hc_irq;

	/* VIRQ numbers of QE high/low irqs */
	unsigned int virq_high;
	unsigned int virq_low;
};

/*
 * QE interrupt controller internal structure
 */
struct qe_ic_info {
	u32	mask;	  /* location of this source at the QIMR register. */
	u32	mask_reg; /* Mask register offset */
	u8	pri_code; /* for grouped interrupts sources - the interrupt
			     code as appears at the group priority register */
	u32	pri_reg;  /* Group priority register offset */
};

#endif /* _POWERPC_SYSDEV_QE_IC_H */
