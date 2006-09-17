/*
 * IPIC private definitions and structure.
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2005 Freescale Semiconductor, Inc
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef __IPIC_H__
#define __IPIC_H__

#include <asm/ipic.h>

#define NR_IPIC_INTS 128

/* External IRQS */
#define IPIC_IRQ_EXT0 48
#define IPIC_IRQ_EXT1 17
#define IPIC_IRQ_EXT7 23

/* Default Priority Registers */
#define IPIC_SIPRR_A_DEFAULT 0x05309770
#define IPIC_SIPRR_D_DEFAULT 0x05309770
#define IPIC_SMPRR_A_DEFAULT 0x05309770
#define IPIC_SMPRR_B_DEFAULT 0x05309770

/* System Global Interrupt Configuration Register */
#define	SICFR_IPSA	0x00010000
#define	SICFR_IPSD	0x00080000
#define	SICFR_MPSA	0x00200000
#define	SICFR_MPSB	0x00400000

/* System External Interrupt Mask Register */
#define	SEMSR_SIRQ0	0x00008000

/* System Error Control Register */
#define SERCR_MCPR	0x00000001

struct ipic {
	volatile u32 __iomem	*regs;

	/* The remapper for this IPIC */
	struct irq_host		*irqhost;

	/* The "linux" controller struct */
	struct irq_chip		hc_irq;

	/* The device node of the interrupt controller */
	struct device_node	*of_node;
};

struct ipic_info {
	u8	pend;		/* pending register offset from base */
	u8	mask;		/* mask register offset from base */
	u8	prio;		/* priority register offset from base */
	u8	force;		/* force register offset from base */
	u8	bit;		/* register bit position (as per doc)
				   bit mask = 1 << (31 - bit) */
	u8	prio_mask;	/* priority mask value */
};

#endif /* __IPIC_H__ */
