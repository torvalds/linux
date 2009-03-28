/* linux/include/asm-arm/plat-s3c24xx/irq.h
 *
 * Copyright (c) 2004-2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Header file for S3C24XX CPU IRQ support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/regs-irq.h>
#include <mach/regs-gpio.h>

#define irqdbf(x...)
#define irqdbf2(x...)

#define EXTINT_OFF (IRQ_EINT4 - 4)

/* these are exported for arch/arm/mach-* usage */
extern struct irq_chip s3c_irq_level_chip;
extern struct irq_chip s3c_irq_chip;

static inline void
s3c_irqsub_mask(unsigned int irqno, unsigned int parentbit,
		int subcheck)
{
	unsigned long mask;
	unsigned long submask;

	submask = __raw_readl(S3C2410_INTSUBMSK);
	mask = __raw_readl(S3C2410_INTMSK);

	submask |= (1UL << (irqno - IRQ_S3CUART_RX0));

	/* check to see if we need to mask the parent IRQ */

	if ((submask  & subcheck) == subcheck) {
		__raw_writel(mask | parentbit, S3C2410_INTMSK);
	}

	/* write back masks */
	__raw_writel(submask, S3C2410_INTSUBMSK);

}

static inline void
s3c_irqsub_unmask(unsigned int irqno, unsigned int parentbit)
{
	unsigned long mask;
	unsigned long submask;

	submask = __raw_readl(S3C2410_INTSUBMSK);
	mask = __raw_readl(S3C2410_INTMSK);

	submask &= ~(1UL << (irqno - IRQ_S3CUART_RX0));
	mask &= ~parentbit;

	/* write back masks */
	__raw_writel(submask, S3C2410_INTSUBMSK);
	__raw_writel(mask, S3C2410_INTMSK);
}


static inline void
s3c_irqsub_maskack(unsigned int irqno, unsigned int parentmask, unsigned int group)
{
	unsigned int bit = 1UL << (irqno - IRQ_S3CUART_RX0);

	s3c_irqsub_mask(irqno, parentmask, group);

	__raw_writel(bit, S3C2410_SUBSRCPND);

	/* only ack parent if we've got all the irqs (seems we must
	 * ack, all and hope that the irq system retriggers ok when
	 * the interrupt goes off again)
	 */

	if (1) {
		__raw_writel(parentmask, S3C2410_SRCPND);
		__raw_writel(parentmask, S3C2410_INTPND);
	}
}

static inline void
s3c_irqsub_ack(unsigned int irqno, unsigned int parentmask, unsigned int group)
{
	unsigned int bit = 1UL << (irqno - IRQ_S3CUART_RX0);

	__raw_writel(bit, S3C2410_SUBSRCPND);

	/* only ack parent if we've got all the irqs (seems we must
	 * ack, all and hope that the irq system retriggers ok when
	 * the interrupt goes off again)
	 */

	if (1) {
		__raw_writel(parentmask, S3C2410_SRCPND);
		__raw_writel(parentmask, S3C2410_INTPND);
	}
}

/* exported for use in arch/arm/mach-s3c2410 */

#ifdef CONFIG_PM
extern int s3c_irq_wake(unsigned int irqno, unsigned int state);
#else
#define s3c_irq_wake NULL
#endif

extern int s3c_irqext_type(unsigned int irq, unsigned int type);
