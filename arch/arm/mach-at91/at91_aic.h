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

#ifndef __ASSEMBLY__
extern void __iomem *at91_aic_base;

#define at91_aic_read(field) \
	__raw_readl(at91_aic_base + field)

#define at91_aic_write(field, value) \
	__raw_writel(value, at91_aic_base + field)
#else
.extern at91_aic_base
#endif

/* Number of irq lines managed by AIC */
#define NR_AIC_IRQS	32
#define NR_AIC5_IRQS	128

#define AT91_AIC5_SSR		0x0			/* Source Select Register [AIC5] */
#define 	AT91_AIC5_INTSEL_MSK	(0x7f << 0)		/* Interrupt Line Selection Mask */

#define AT91_AIC_IRQ_MIN_PRIORITY	0
#define AT91_AIC_IRQ_MAX_PRIORITY	7

#define AT91_AIC_SMR(n)		((n) * 4)		/* Source Mode Registers 0-31 */
#define AT91_AIC5_SMR		0x4			/* Source Mode Register [AIC5] */
#define		AT91_AIC_PRIOR		(7 << 0)		/* Priority Level */
#define		AT91_AIC_SRCTYPE	(3 << 5)		/* Interrupt Source Type */
#define			AT91_AIC_SRCTYPE_LOW		(0 << 5)
#define			AT91_AIC_SRCTYPE_FALLING	(1 << 5)
#define			AT91_AIC_SRCTYPE_HIGH		(2 << 5)
#define			AT91_AIC_SRCTYPE_RISING		(3 << 5)

#define AT91_AIC_SVR(n)		(0x80 + ((n) * 4))	/* Source Vector Registers 0-31 */
#define AT91_AIC5_SVR		0x8			/* Source Vector Register [AIC5] */
#define AT91_AIC_IVR		0x100			/* Interrupt Vector Register */
#define AT91_AIC5_IVR		0x10			/* Interrupt Vector Register [AIC5] */
#define AT91_AIC_FVR		0x104			/* Fast Interrupt Vector Register */
#define AT91_AIC5_FVR		0x14			/* Fast Interrupt Vector Register [AIC5] */
#define AT91_AIC_ISR		0x108			/* Interrupt Status Register */
#define AT91_AIC5_ISR		0x18			/* Interrupt Status Register [AIC5] */
#define		AT91_AIC_IRQID		(0x1f << 0)		/* Current Interrupt Identifier */

#define AT91_AIC_IPR		0x10c			/* Interrupt Pending Register */
#define AT91_AIC5_IPR0		0x20			/* Interrupt Pending Register 0 [AIC5] */
#define AT91_AIC5_IPR1		0x24			/* Interrupt Pending Register 1 [AIC5] */
#define AT91_AIC5_IPR2		0x28			/* Interrupt Pending Register 2 [AIC5] */
#define AT91_AIC5_IPR3		0x2c			/* Interrupt Pending Register 3 [AIC5] */
#define AT91_AIC_IMR		0x110			/* Interrupt Mask Register */
#define AT91_AIC5_IMR		0x30			/* Interrupt Mask Register [AIC5] */
#define AT91_AIC_CISR		0x114			/* Core Interrupt Status Register */
#define AT91_AIC5_CISR		0x34			/* Core Interrupt Status Register [AIC5] */
#define		AT91_AIC_NFIQ		(1 << 0)		/* nFIQ Status */
#define		AT91_AIC_NIRQ		(1 << 1)		/* nIRQ Status */

#define AT91_AIC_IECR		0x120			/* Interrupt Enable Command Register */
#define AT91_AIC5_IECR		0x40			/* Interrupt Enable Command Register [AIC5] */
#define AT91_AIC_IDCR		0x124			/* Interrupt Disable Command Register */
#define AT91_AIC5_IDCR		0x44			/* Interrupt Disable Command Register [AIC5] */
#define AT91_AIC_ICCR		0x128			/* Interrupt Clear Command Register */
#define AT91_AIC5_ICCR		0x48			/* Interrupt Clear Command Register [AIC5] */
#define AT91_AIC_ISCR		0x12c			/* Interrupt Set Command Register */
#define AT91_AIC5_ISCR		0x4c			/* Interrupt Set Command Register [AIC5] */
#define AT91_AIC_EOICR		0x130			/* End of Interrupt Command Register */
#define AT91_AIC5_EOICR		0x38			/* End of Interrupt Command Register [AIC5] */
#define AT91_AIC_SPU		0x134			/* Spurious Interrupt Vector Register */
#define AT91_AIC5_SPU		0x3c			/* Spurious Interrupt Vector Register [AIC5] */
#define AT91_AIC_DCR		0x138			/* Debug Control Register */
#define AT91_AIC5_DCR		0x6c			/* Debug Control Register [AIC5] */
#define		AT91_AIC_DCR_PROT	(1 << 0)		/* Protection Mode */
#define		AT91_AIC_DCR_GMSK	(1 << 1)		/* General Mask */

#define AT91_AIC_FFER		0x140			/* Fast Forcing Enable Register [SAM9 only] */
#define AT91_AIC5_FFER		0x50			/* Fast Forcing Enable Register [AIC5] */
#define AT91_AIC_FFDR		0x144			/* Fast Forcing Disable Register [SAM9 only] */
#define AT91_AIC5_FFDR		0x54			/* Fast Forcing Disable Register [AIC5] */
#define AT91_AIC_FFSR		0x148			/* Fast Forcing Status Register [SAM9 only] */
#define AT91_AIC5_FFSR		0x58			/* Fast Forcing Status Register [AIC5] */

void at91_aic_handle_irq(struct pt_regs *regs);
void at91_aic5_handle_irq(struct pt_regs *regs);

#endif
