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

#define MPC83xx_IPIC_SIZE	(0x00100)

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
	unsigned int		irq_offset;
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
