/*
 * arch/arm/mach-at91/include/mach/at91_aic.h
 *
 * Copyright (C) 2005 Ivan Kokshaysky
 * Copyright (C) SAN People
 *
 * Advanced Interrupt Controller (AIC) - System peripherals registers.
 * Based on AT91RM9200 datasheet revision E.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91_AIC_H
#define AT91_AIC_H

#define AT91_AIC_SMR(n)		(AT91_AIC + ((n) * 4))	/* Source Mode Registers 0-31 */
#define		AT91_AIC_PRIOR		(7 << 0)		/* Priority Level */
#define		AT91_AIC_SRCTYPE	(3 << 5)		/* Interrupt Source Type */
#define			AT91_AIC_SRCTYPE_LOW		(0 << 5)
#define			AT91_AIC_SRCTYPE_FALLING	(1 << 5)
#define			AT91_AIC_SRCTYPE_HIGH		(2 << 5)
#define			AT91_AIC_SRCTYPE_RISING		(3 << 5)

#define AT91_AIC_SVR(n)		(AT91_AIC + 0x80 + ((n) * 4))	/* Source Vector Registers 0-31 */
#define AT91_AIC_IVR		(AT91_AIC + 0x100)	/* Interrupt Vector Register */
#define AT91_AIC_FVR		(AT91_AIC + 0x104)	/* Fast Interrupt Vector Register */
#define AT91_AIC_ISR		(AT91_AIC + 0x108)	/* Interrupt Status Register */
#define		AT91_AIC_IRQID		(0x1f << 0)		/* Current Interrupt Identifier */

#define AT91_AIC_IPR		(AT91_AIC + 0x10c)	/* Interrupt Pending Register */
#define AT91_AIC_IMR		(AT91_AIC + 0x110)	/* Interrupt Mask Register */
#define AT91_AIC_CISR		(AT91_AIC + 0x114)	/* Core Interrupt Status Register */
#define		AT91_AIC_NFIQ		(1 << 0)		/* nFIQ Status */
#define		AT91_AIC_NIRQ		(1 << 1)		/* nIRQ Status */

#define AT91_AIC_IECR		(AT91_AIC + 0x120)	/* Interrupt Enable Command Register */
#define AT91_AIC_IDCR		(AT91_AIC + 0x124)	/* Interrupt Disable Command Register */
#define AT91_AIC_ICCR		(AT91_AIC + 0x128)	/* Interrupt Clear Command Register */
#define AT91_AIC_ISCR		(AT91_AIC + 0x12c)	/* Interrupt Set Command Register */
#define AT91_AIC_EOICR		(AT91_AIC + 0x130)	/* End of Interrupt Command Register */
#define AT91_AIC_SPU		(AT91_AIC + 0x134)	/* Spurious Interrupt Vector Register */
#define AT91_AIC_DCR		(AT91_AIC + 0x138)	/* Debug Control Register */
#define		AT91_AIC_DCR_PROT	(1 << 0)		/* Protection Mode */
#define		AT91_AIC_DCR_GMSK	(1 << 1)		/* General Mask */

#define AT91_AIC_FFER		(AT91_AIC + 0x140)	/* Fast Forcing Enable Register [SAM9 only] */
#define AT91_AIC_FFDR		(AT91_AIC + 0x144)	/* Fast Forcing Disable Register [SAM9 only] */
#define AT91_AIC_FFSR		(AT91_AIC + 0x148)	/* Fast Forcing Status Register [SAM9 only] */

#endif
