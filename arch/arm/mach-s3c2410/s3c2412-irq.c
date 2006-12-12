/* linux/arch/arm/mach-s3c2412/s3c2412-irq.c
 *
 * Copyright (c) 2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/ptrace.h>
#include <linux/sysdev.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <asm/mach/irq.h>

#include <asm/arch/regs-irq.h>
#include <asm/arch/regs-gpio.h>

#include "cpu.h"
#include "irq.h"
#include "pm.h"

/* the s3c2412 changes the behaviour of IRQ_EINT0 through IRQ_EINT3 by
 * having them turn up in both the INT* and the EINT* registers. Whilst
 * both show the status, they both now need to be acked when the IRQs
 * go off.
*/

static void
s3c2412_irq_mask(unsigned int irqno)
{
	unsigned long bitval = 1UL << (irqno - IRQ_EINT0);
	unsigned long mask;

	mask = __raw_readl(S3C2410_INTMSK);
	__raw_writel(mask | bitval, S3C2410_INTMSK);

	mask = __raw_readl(S3C2412_EINTMASK);
	__raw_writel(mask | bitval, S3C2412_EINTMASK);
}

static inline void
s3c2412_irq_ack(unsigned int irqno)
{
	unsigned long bitval = 1UL << (irqno - IRQ_EINT0);

	__raw_writel(bitval, S3C2412_EINTPEND);
	__raw_writel(bitval, S3C2410_SRCPND);
	__raw_writel(bitval, S3C2410_INTPND);
}

static inline void
s3c2412_irq_maskack(unsigned int irqno)
{
	unsigned long bitval = 1UL << (irqno - IRQ_EINT0);
	unsigned long mask;

	mask = __raw_readl(S3C2410_INTMSK);
	__raw_writel(mask|bitval, S3C2410_INTMSK);

	mask = __raw_readl(S3C2412_EINTMASK);
	__raw_writel(mask | bitval, S3C2412_EINTMASK);

	__raw_writel(bitval, S3C2412_EINTPEND);
	__raw_writel(bitval, S3C2410_SRCPND);
	__raw_writel(bitval, S3C2410_INTPND);
}

static void
s3c2412_irq_unmask(unsigned int irqno)
{
	unsigned long bitval = 1UL << (irqno - IRQ_EINT0);
	unsigned long mask;

	mask = __raw_readl(S3C2412_EINTMASK);
	__raw_writel(mask & ~bitval, S3C2412_EINTMASK);

	mask = __raw_readl(S3C2410_INTMSK);
	__raw_writel(mask & ~bitval, S3C2410_INTMSK);
}

static struct irq_chip s3c2412_irq_eint0t4 = {
	.ack	   = s3c2412_irq_ack,
	.mask	   = s3c2412_irq_mask,
	.unmask	   = s3c2412_irq_unmask,
	.set_wake  = s3c_irq_wake,
	.set_type  = s3c_irqext_type,
};

static int s3c2412_irq_add(struct sys_device *sysdev)
{
	unsigned int irqno;

	for (irqno = IRQ_EINT0; irqno <= IRQ_EINT3; irqno++) {
		set_irq_chip(irqno, &s3c2412_irq_eint0t4);
		set_irq_handler(irqno, handle_edge_irq);
		set_irq_flags(irqno, IRQF_VALID);
	}

	return 0;
}

static struct sysdev_driver s3c2412_irq_driver = {
	.add		= s3c2412_irq_add,
	.suspend	= s3c24xx_irq_suspend,
	.resume		= s3c24xx_irq_resume,
};

static int s3c2412_irq_init(void)
{
	return sysdev_driver_register(&s3c2412_sysclass, &s3c2412_irq_driver);
}

arch_initcall(s3c2412_irq_init);
