/* linux/arch/arm/mach-s3c2412/irq.c
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
#include <linux/device.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/irq.h>

#include <asm/mach/irq.h>

#include <mach/regs-irq.h>
#include <mach/regs-gpio.h>
#include <mach/regs-power.h>

#include <plat/cpu.h>
#include <plat/irq.h>
#include <plat/pm.h>

#define INTMSK(start, end) ((1 << ((end) + 1 - (start))) - 1)
#define INTMSK_SUB(start, end) (INTMSK(start, end) << ((start - S3C2410_IRQSUB(0))))

/* the s3c2412 changes the behaviour of IRQ_EINT0 through IRQ_EINT3 by
 * having them turn up in both the INT* and the EINT* registers. Whilst
 * both show the status, they both now need to be acked when the IRQs
 * go off.
*/

static void
s3c2412_irq_mask(struct irq_data *data)
{
	unsigned long bitval = 1UL << (data->irq - IRQ_EINT0);
	unsigned long mask;

	mask = __raw_readl(S3C2410_INTMSK);
	__raw_writel(mask | bitval, S3C2410_INTMSK);

	mask = __raw_readl(S3C2412_EINTMASK);
	__raw_writel(mask | bitval, S3C2412_EINTMASK);
}

static inline void
s3c2412_irq_ack(struct irq_data *data)
{
	unsigned long bitval = 1UL << (data->irq - IRQ_EINT0);

	__raw_writel(bitval, S3C2412_EINTPEND);
	__raw_writel(bitval, S3C2410_SRCPND);
	__raw_writel(bitval, S3C2410_INTPND);
}

static inline void
s3c2412_irq_maskack(struct irq_data *data)
{
	unsigned long bitval = 1UL << (data->irq - IRQ_EINT0);
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
s3c2412_irq_unmask(struct irq_data *data)
{
	unsigned long bitval = 1UL << (data->irq - IRQ_EINT0);
	unsigned long mask;

	mask = __raw_readl(S3C2412_EINTMASK);
	__raw_writel(mask & ~bitval, S3C2412_EINTMASK);

	mask = __raw_readl(S3C2410_INTMSK);
	__raw_writel(mask & ~bitval, S3C2410_INTMSK);
}

static struct irq_chip s3c2412_irq_eint0t4 = {
	.irq_ack	= s3c2412_irq_ack,
	.irq_mask	= s3c2412_irq_mask,
	.irq_unmask	= s3c2412_irq_unmask,
	.irq_set_wake	= s3c_irq_wake,
	.irq_set_type	= s3c_irqext_type,
};

#define INTBIT(x)	(1 << ((x) - S3C2410_IRQSUB(0)))

/* CF and SDI sub interrupts */

static void s3c2412_irq_demux_cfsdi(unsigned int irq, struct irq_desc *desc)
{
	unsigned int subsrc, submsk;

	subsrc = __raw_readl(S3C2410_SUBSRCPND);
	submsk = __raw_readl(S3C2410_INTSUBMSK);

	subsrc  &= ~submsk;

	if (subsrc & INTBIT(IRQ_S3C2412_SDI))
		generic_handle_irq(IRQ_S3C2412_SDI);

	if (subsrc & INTBIT(IRQ_S3C2412_CF))
		generic_handle_irq(IRQ_S3C2412_CF);
}

#define INTMSK_CFSDI	(1UL << (IRQ_S3C2412_CFSDI - IRQ_EINT0))
#define SUBMSK_CFSDI	INTMSK_SUB(IRQ_S3C2412_SDI, IRQ_S3C2412_CF)

static void s3c2412_irq_cfsdi_mask(struct irq_data *data)
{
	s3c_irqsub_mask(data->irq, INTMSK_CFSDI, SUBMSK_CFSDI);
}

static void s3c2412_irq_cfsdi_unmask(struct irq_data *data)
{
	s3c_irqsub_unmask(data->irq, INTMSK_CFSDI);
}

static void s3c2412_irq_cfsdi_ack(struct irq_data *data)
{
	s3c_irqsub_maskack(data->irq, INTMSK_CFSDI, SUBMSK_CFSDI);
}

static struct irq_chip s3c2412_irq_cfsdi = {
	.name		= "s3c2412-cfsdi",
	.irq_ack	= s3c2412_irq_cfsdi_ack,
	.irq_mask	= s3c2412_irq_cfsdi_mask,
	.irq_unmask	= s3c2412_irq_cfsdi_unmask,
};

static int s3c2412_irq_rtc_wake(struct irq_data *data, unsigned int state)
{
	unsigned long pwrcfg;

	pwrcfg = __raw_readl(S3C2412_PWRCFG);
	if (state)
		pwrcfg &= ~S3C2412_PWRCFG_RTC_MASKIRQ;
	else
		pwrcfg |= S3C2412_PWRCFG_RTC_MASKIRQ;
	__raw_writel(pwrcfg, S3C2412_PWRCFG);

	return s3c_irq_chip.irq_set_wake(data, state);
}

static struct irq_chip s3c2412_irq_rtc_chip;

static int s3c2412_irq_add(struct device *dev, struct subsys_interface *sif)
{
	unsigned int irqno;

	for (irqno = IRQ_EINT0; irqno <= IRQ_EINT3; irqno++) {
		irq_set_chip_and_handler(irqno, &s3c2412_irq_eint0t4,
					 handle_edge_irq);
		set_irq_flags(irqno, IRQF_VALID);
	}

	/* add demux support for CF/SDI */

	irq_set_chained_handler(IRQ_S3C2412_CFSDI, s3c2412_irq_demux_cfsdi);

	for (irqno = IRQ_S3C2412_SDI; irqno <= IRQ_S3C2412_CF; irqno++) {
		irq_set_chip_and_handler(irqno, &s3c2412_irq_cfsdi,
					 handle_level_irq);
		set_irq_flags(irqno, IRQF_VALID);
	}

	/* change RTC IRQ's set wake method */

	s3c2412_irq_rtc_chip = s3c_irq_chip;
	s3c2412_irq_rtc_chip.irq_set_wake = s3c2412_irq_rtc_wake;

	irq_set_chip(IRQ_RTC, &s3c2412_irq_rtc_chip);

	return 0;
}

static struct subsys_interface s3c2412_irq_interface = {
	.name		= "s3c2412_irq",
	.subsys		= &s3c2412_subsys,
	.add_dev	= s3c2412_irq_add,
};

static int s3c2412_irq_init(void)
{
	return subsys_interface_register(&s3c2412_irq_interface);
}

arch_initcall(s3c2412_irq_init);
