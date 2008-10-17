/* linux/arch/arm/mach-s3c2440/irq.c
 *
 * Copyright (c) 2003,2004 Simtec Electronics
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
#include <linux/sysdev.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/irq.h>

#include <asm/mach/irq.h>

#include <mach/regs-irq.h>
#include <mach/regs-gpio.h>

#include <plat/cpu.h>
#include <plat/pm.h>
#include <plat/irq.h>

/* WDT/AC97 */

static void s3c_irq_demux_wdtac97(unsigned int irq,
				  struct irq_desc *desc)
{
	unsigned int subsrc, submsk;

	/* read the current pending interrupts, and the mask
	 * for what it is available */

	subsrc = __raw_readl(S3C2410_SUBSRCPND);
	submsk = __raw_readl(S3C2410_INTSUBMSK);

	subsrc &= ~submsk;
	subsrc >>= 13;
	subsrc &= 3;

	if (subsrc != 0) {
		if (subsrc & 1) {
			generic_handle_irq(IRQ_S3C2440_WDT);
		}
		if (subsrc & 2) {
			generic_handle_irq(IRQ_S3C2440_AC97);
		}
	}
}


#define INTMSK_WDT	 (1UL << (IRQ_WDT - IRQ_EINT0))

static void
s3c_irq_wdtac97_mask(unsigned int irqno)
{
	s3c_irqsub_mask(irqno, INTMSK_WDT, 3<<13);
}

static void
s3c_irq_wdtac97_unmask(unsigned int irqno)
{
	s3c_irqsub_unmask(irqno, INTMSK_WDT);
}

static void
s3c_irq_wdtac97_ack(unsigned int irqno)
{
	s3c_irqsub_maskack(irqno, INTMSK_WDT, 3<<13);
}

static struct irq_chip s3c_irq_wdtac97 = {
	.mask	    = s3c_irq_wdtac97_mask,
	.unmask	    = s3c_irq_wdtac97_unmask,
	.ack	    = s3c_irq_wdtac97_ack,
};

static int s3c2440_irq_add(struct sys_device *sysdev)
{
	unsigned int irqno;

	printk("S3C2440: IRQ Support\n");

	/* add new chained handler for wdt, ac7 */

	set_irq_chip(IRQ_WDT, &s3c_irq_level_chip);
	set_irq_handler(IRQ_WDT, handle_level_irq);
	set_irq_chained_handler(IRQ_WDT, s3c_irq_demux_wdtac97);

	for (irqno = IRQ_S3C2440_WDT; irqno <= IRQ_S3C2440_AC97; irqno++) {
		set_irq_chip(irqno, &s3c_irq_wdtac97);
		set_irq_handler(irqno, handle_level_irq);
		set_irq_flags(irqno, IRQF_VALID);
	}

	return 0;
}

static struct sysdev_driver s3c2440_irq_driver = {
	.add		= s3c2440_irq_add,
};

static int s3c2440_irq_init(void)
{
	return sysdev_driver_register(&s3c2440_sysclass, &s3c2440_irq_driver);
}

arch_initcall(s3c2440_irq_init);

