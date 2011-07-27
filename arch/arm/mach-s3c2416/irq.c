/* linux/arch/arm/mach-s3c2416/irq.c
 *
 * Copyright (c) 2009 Yauhen Kharuzhy <jekhor@gmail.com>,
 *	as part of OpenInkpot project
 * Copyright (c) 2009 Promwad Innovation Company
 *	Yauhen Kharuzhy <yauhen.kharuzhy@promwad.com>
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

#define INTMSK(start, end) ((1 << ((end) + 1 - (start))) - 1)

static inline void s3c2416_irq_demux(unsigned int irq, unsigned int len)
{
	unsigned int subsrc, submsk;
	unsigned int end;

	/* read the current pending interrupts, and the mask
	 * for what it is available */

	subsrc = __raw_readl(S3C2410_SUBSRCPND);
	submsk = __raw_readl(S3C2410_INTSUBMSK);

	subsrc  &= ~submsk;
	subsrc >>= (irq - S3C2410_IRQSUB(0));
	subsrc  &= (1 << len)-1;

	end = len + irq;

	for (; irq < end && subsrc; irq++) {
		if (subsrc & 1)
			generic_handle_irq(irq);

		subsrc >>= 1;
	}
}

/* WDT/AC97 sub interrupts */

static void s3c2416_irq_demux_wdtac97(unsigned int irq, struct irq_desc *desc)
{
	s3c2416_irq_demux(IRQ_S3C2443_WDT, 4);
}

#define INTMSK_WDTAC97	(1UL << (IRQ_WDT - IRQ_EINT0))
#define SUBMSK_WDTAC97	INTMSK(IRQ_S3C2443_WDT, IRQ_S3C2443_AC97)

static void s3c2416_irq_wdtac97_mask(struct irq_data *data)
{
	s3c_irqsub_mask(data->irq, INTMSK_WDTAC97, SUBMSK_WDTAC97);
}

static void s3c2416_irq_wdtac97_unmask(struct irq_data *data)
{
	s3c_irqsub_unmask(data->irq, INTMSK_WDTAC97);
}

static void s3c2416_irq_wdtac97_ack(struct irq_data *data)
{
	s3c_irqsub_maskack(data->irq, INTMSK_WDTAC97, SUBMSK_WDTAC97);
}

static struct irq_chip s3c2416_irq_wdtac97 = {
	.irq_mask	= s3c2416_irq_wdtac97_mask,
	.irq_unmask	= s3c2416_irq_wdtac97_unmask,
	.irq_ack	= s3c2416_irq_wdtac97_ack,
};

/* LCD sub interrupts */

static void s3c2416_irq_demux_lcd(unsigned int irq, struct irq_desc *desc)
{
	s3c2416_irq_demux(IRQ_S3C2443_LCD1, 4);
}

#define INTMSK_LCD	(1UL << (IRQ_LCD - IRQ_EINT0))
#define SUBMSK_LCD	INTMSK(IRQ_S3C2443_LCD1, IRQ_S3C2443_LCD4)

static void s3c2416_irq_lcd_mask(struct irq_data *data)
{
	s3c_irqsub_mask(data->irq, INTMSK_LCD, SUBMSK_LCD);
}

static void s3c2416_irq_lcd_unmask(struct irq_data *data)
{
	s3c_irqsub_unmask(data->irq, INTMSK_LCD);
}

static void s3c2416_irq_lcd_ack(struct irq_data *data)
{
	s3c_irqsub_maskack(data->irq, INTMSK_LCD, SUBMSK_LCD);
}

static struct irq_chip s3c2416_irq_lcd = {
	.irq_mask	= s3c2416_irq_lcd_mask,
	.irq_unmask	= s3c2416_irq_lcd_unmask,
	.irq_ack	= s3c2416_irq_lcd_ack,
};

/* DMA sub interrupts */

static void s3c2416_irq_demux_dma(unsigned int irq, struct irq_desc *desc)
{
	s3c2416_irq_demux(IRQ_S3C2443_DMA0, 6);
}

#define INTMSK_DMA	(1UL << (IRQ_S3C2443_DMA - IRQ_EINT0))
#define SUBMSK_DMA	INTMSK(IRQ_S3C2443_DMA0, IRQ_S3C2443_DMA5)


static void s3c2416_irq_dma_mask(struct irq_data *data)
{
	s3c_irqsub_mask(data->irq, INTMSK_DMA, SUBMSK_DMA);
}

static void s3c2416_irq_dma_unmask(struct irq_data *data)
{
	s3c_irqsub_unmask(data->irq, INTMSK_DMA);
}

static void s3c2416_irq_dma_ack(struct irq_data *data)
{
	s3c_irqsub_maskack(data->irq, INTMSK_DMA, SUBMSK_DMA);
}

static struct irq_chip s3c2416_irq_dma = {
	.irq_mask	= s3c2416_irq_dma_mask,
	.irq_unmask	= s3c2416_irq_dma_unmask,
	.irq_ack	= s3c2416_irq_dma_ack,
};

/* UART3 sub interrupts */

static void s3c2416_irq_demux_uart3(unsigned int irq, struct irq_desc *desc)
{
	s3c2416_irq_demux(IRQ_S3C2443_RX3, 3);
}

#define INTMSK_UART3	(1UL << (IRQ_S3C2443_UART3 - IRQ_EINT0))
#define SUBMSK_UART3	(0x7 << (IRQ_S3C2443_RX3 - S3C2410_IRQSUB(0)))

static void s3c2416_irq_uart3_mask(struct irq_data *data)
{
	s3c_irqsub_mask(data->irq, INTMSK_UART3, SUBMSK_UART3);
}

static void s3c2416_irq_uart3_unmask(struct irq_data *data)
{
	s3c_irqsub_unmask(data->irq, INTMSK_UART3);
}

static void s3c2416_irq_uart3_ack(struct irq_data *data)
{
	s3c_irqsub_maskack(data->irq, INTMSK_UART3, SUBMSK_UART3);
}

static struct irq_chip s3c2416_irq_uart3 = {
	.irq_mask	= s3c2416_irq_uart3_mask,
	.irq_unmask	= s3c2416_irq_uart3_unmask,
	.irq_ack	= s3c2416_irq_uart3_ack,
};

/* IRQ initialisation code */

static int __init s3c2416_add_sub(unsigned int base,
				   void (*demux)(unsigned int,
						 struct irq_desc *),
				   struct irq_chip *chip,
				   unsigned int start, unsigned int end)
{
	unsigned int irqno;

	irq_set_chip_and_handler(base, &s3c_irq_level_chip, handle_level_irq);
	irq_set_chained_handler(base, demux);

	for (irqno = start; irqno <= end; irqno++) {
		irq_set_chip_and_handler(irqno, chip, handle_level_irq);
		set_irq_flags(irqno, IRQF_VALID);
	}

	return 0;
}

static int __init s3c2416_irq_add(struct sys_device *sysdev)
{
	printk(KERN_INFO "S3C2416: IRQ Support\n");

	s3c2416_add_sub(IRQ_LCD, s3c2416_irq_demux_lcd, &s3c2416_irq_lcd,
			IRQ_S3C2443_LCD2, IRQ_S3C2443_LCD4);

	s3c2416_add_sub(IRQ_S3C2443_DMA, s3c2416_irq_demux_dma,
			&s3c2416_irq_dma, IRQ_S3C2443_DMA0, IRQ_S3C2443_DMA5);

	s3c2416_add_sub(IRQ_S3C2443_UART3, s3c2416_irq_demux_uart3,
			&s3c2416_irq_uart3,
			IRQ_S3C2443_RX3, IRQ_S3C2443_ERR3);

	s3c2416_add_sub(IRQ_WDT, s3c2416_irq_demux_wdtac97,
			&s3c2416_irq_wdtac97,
			IRQ_S3C2443_WDT, IRQ_S3C2443_AC97);

	return 0;
}

static struct sysdev_driver s3c2416_irq_driver = {
	.add		= s3c2416_irq_add,
};

static int __init s3c2416_irq_init(void)
{
	return sysdev_driver_register(&s3c2416_sysclass, &s3c2416_irq_driver);
}

arch_initcall(s3c2416_irq_init);

