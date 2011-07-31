/* linux/arch/arm/plat-s3c24xx/s3c244x-irq.c
 *
 * Copyright (c) 2003-2004 Simtec Electronics
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

/* camera irq */

static void s3c_irq_demux_cam(unsigned int irq,
			      struct irq_desc *desc)
{
	unsigned int subsrc, submsk;

	/* read the current pending interrupts, and the mask
	 * for what it is available */

	subsrc = __raw_readl(S3C2410_SUBSRCPND);
	submsk = __raw_readl(S3C2410_INTSUBMSK);

	subsrc &= ~submsk;
	subsrc >>= 11;
	subsrc &= 3;

	if (subsrc != 0) {
		if (subsrc & 1) {
			generic_handle_irq(IRQ_S3C2440_CAM_C);
		}
		if (subsrc & 2) {
			generic_handle_irq(IRQ_S3C2440_CAM_P);
		}
	}
}

#define INTMSK_CAM (1UL << (IRQ_CAM - IRQ_EINT0))

static void
s3c_irq_cam_mask(unsigned int irqno)
{
	s3c_irqsub_mask(irqno, INTMSK_CAM, 3<<11);
}

static void
s3c_irq_cam_unmask(unsigned int irqno)
{
	s3c_irqsub_unmask(irqno, INTMSK_CAM);
}

static void
s3c_irq_cam_ack(unsigned int irqno)
{
	s3c_irqsub_maskack(irqno, INTMSK_CAM, 3<<11);
}

static struct irq_chip s3c_irq_cam = {
	.mask	    = s3c_irq_cam_mask,
	.unmask	    = s3c_irq_cam_unmask,
	.ack	    = s3c_irq_cam_ack,
};

static int s3c244x_irq_add(struct sys_device *sysdev)
{
	unsigned int irqno;

	set_irq_chip(IRQ_NFCON, &s3c_irq_level_chip);
	set_irq_handler(IRQ_NFCON, handle_level_irq);
	set_irq_flags(IRQ_NFCON, IRQF_VALID);

	/* add chained handler for camera */

	set_irq_chip(IRQ_CAM, &s3c_irq_level_chip);
	set_irq_handler(IRQ_CAM, handle_level_irq);
	set_irq_chained_handler(IRQ_CAM, s3c_irq_demux_cam);

	for (irqno = IRQ_S3C2440_CAM_C; irqno <= IRQ_S3C2440_CAM_P; irqno++) {
		set_irq_chip(irqno, &s3c_irq_cam);
		set_irq_handler(irqno, handle_level_irq);
		set_irq_flags(irqno, IRQF_VALID);
	}

	return 0;
}

static struct sysdev_driver s3c2440_irq_driver = {
	.add		= s3c244x_irq_add,
	.suspend	= s3c24xx_irq_suspend,
	.resume		= s3c24xx_irq_resume,
};

static int s3c2440_irq_init(void)
{
	return sysdev_driver_register(&s3c2440_sysclass, &s3c2440_irq_driver);
}

arch_initcall(s3c2440_irq_init);

static struct sysdev_driver s3c2442_irq_driver = {
	.add		= s3c244x_irq_add,
	.suspend	= s3c24xx_irq_suspend,
	.resume		= s3c24xx_irq_resume,
};


static int s3c2442_irq_init(void)
{
	return sysdev_driver_register(&s3c2442_sysclass, &s3c2442_irq_driver);
}

arch_initcall(s3c2442_irq_init);
