/*
 *  linux/arch/arm/mach-pxa/irq.c
 *
 *  Generic PXA IRQ handling
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/sysdev.h>
#include <linux/io.h>
#include <linux/irq.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/gpio.h>

#include "generic.h"

#define IRQ_BASE		(void __iomem *)io_p2v(0x40d00000)

#define ICIP			(0x000)
#define ICMR			(0x004)
#define ICLR			(0x008)
#define ICFR			(0x00c)
#define ICPR			(0x010)
#define ICCR			(0x014)
#define ICHP			(0x018)
#define IPR(i)			(((i) < 32) ? (0x01c + ((i) << 2)) :		\
				((i) < 64) ? (0x0b0 + (((i) - 32) << 2)) :	\
				      (0x144 + (((i) - 64) << 2)))
#define IPR_VALID		(1 << 31)
#define IRQ_BIT(n)		(((n) - PXA_IRQ(0)) & 0x1f)

#define MAX_INTERNAL_IRQS	128

/*
 * This is for peripheral IRQs internal to the PXA chip.
 */

static int pxa_internal_irq_nr;

static inline int cpu_has_ipr(void)
{
	return !cpu_is_pxa25x();
}

static inline void __iomem *irq_base(int i)
{
	static unsigned long phys_base[] = {
		0x40d00000,
		0x40d0009c,
		0x40d00130,
	};

	return (void __iomem *)io_p2v(phys_base[i]);
}

static void pxa_mask_irq(struct irq_data *d)
{
	void __iomem *base = irq_data_get_irq_chip_data(d);
	uint32_t icmr = __raw_readl(base + ICMR);

	icmr &= ~(1 << IRQ_BIT(d->irq));
	__raw_writel(icmr, base + ICMR);
}

static void pxa_unmask_irq(struct irq_data *d)
{
	void __iomem *base = irq_data_get_irq_chip_data(d);
	uint32_t icmr = __raw_readl(base + ICMR);

	icmr |= 1 << IRQ_BIT(d->irq);
	__raw_writel(icmr, base + ICMR);
}

static struct irq_chip pxa_internal_irq_chip = {
	.name		= "SC",
	.irq_ack	= pxa_mask_irq,
	.irq_mask	= pxa_mask_irq,
	.irq_unmask	= pxa_unmask_irq,
};

/*
 * GPIO IRQs for GPIO 0 and 1
 */
static int pxa_set_low_gpio_type(struct irq_data *d, unsigned int type)
{
	int gpio = d->irq - IRQ_GPIO0;

	if (__gpio_is_occupied(gpio)) {
		pr_err("%s failed: GPIO is configured\n", __func__);
		return -EINVAL;
	}

	if (type & IRQ_TYPE_EDGE_RISING)
		GRER0 |= GPIO_bit(gpio);
	else
		GRER0 &= ~GPIO_bit(gpio);

	if (type & IRQ_TYPE_EDGE_FALLING)
		GFER0 |= GPIO_bit(gpio);
	else
		GFER0 &= ~GPIO_bit(gpio);

	return 0;
}

static void pxa_ack_low_gpio(struct irq_data *d)
{
	GEDR0 = (1 << (d->irq - IRQ_GPIO0));
}

static struct irq_chip pxa_low_gpio_chip = {
	.name		= "GPIO-l",
	.irq_ack	= pxa_ack_low_gpio,
	.irq_mask	= pxa_mask_irq,
	.irq_unmask	= pxa_unmask_irq,
	.irq_set_type	= pxa_set_low_gpio_type,
};

static void __init pxa_init_low_gpio_irq(set_wake_t fn)
{
	int irq;

	/* clear edge detection on GPIO 0 and 1 */
	GFER0 &= ~0x3;
	GRER0 &= ~0x3;
	GEDR0 = 0x3;

	for (irq = IRQ_GPIO0; irq <= IRQ_GPIO1; irq++) {
		irq_set_chip_and_handler(irq, &pxa_low_gpio_chip,
					 handle_edge_irq);
		irq_set_chip_data(irq, irq_base(0));
		set_irq_flags(irq, IRQF_VALID);
	}

	pxa_low_gpio_chip.irq_set_wake = fn;
}

void __init pxa_init_irq(int irq_nr, set_wake_t fn)
{
	int irq, i, n;

	BUG_ON(irq_nr > MAX_INTERNAL_IRQS);

	pxa_internal_irq_nr = irq_nr;

	for (n = 0; n < irq_nr; n += 32) {
		void __iomem *base = irq_base(n >> 5);

		__raw_writel(0, base + ICMR);	/* disable all IRQs */
		__raw_writel(0, base + ICLR);	/* all IRQs are IRQ, not FIQ */
		for (i = n; (i < (n + 32)) && (i < irq_nr); i++) {
			/* initialize interrupt priority */
			if (cpu_has_ipr())
				__raw_writel(i | IPR_VALID, IRQ_BASE + IPR(i));

			irq = PXA_IRQ(i);
			irq_set_chip_and_handler(irq, &pxa_internal_irq_chip,
						 handle_level_irq);
			irq_set_chip_data(irq, base);
			set_irq_flags(irq, IRQF_VALID);
		}
	}

	/* only unmasked interrupts kick us out of idle */
	__raw_writel(1, irq_base(0) + ICCR);

	pxa_internal_irq_chip.irq_set_wake = fn;
	pxa_init_low_gpio_irq(fn);
}

#ifdef CONFIG_PM
static unsigned long saved_icmr[MAX_INTERNAL_IRQS/32];
static unsigned long saved_ipr[MAX_INTERNAL_IRQS];

static int pxa_irq_suspend(struct sys_device *dev, pm_message_t state)
{
	int i;

	for (i = 0; i < pxa_internal_irq_nr / 32; i++) {
		void __iomem *base = irq_base(i);

		saved_icmr[i] = __raw_readl(base + ICMR);
		__raw_writel(0, base + ICMR);
	}

	if (cpu_has_ipr()) {
		for (i = 0; i < pxa_internal_irq_nr; i++)
			saved_ipr[i] = __raw_readl(IRQ_BASE + IPR(i));
	}

	return 0;
}

static int pxa_irq_resume(struct sys_device *dev)
{
	int i;

	for (i = 0; i < pxa_internal_irq_nr / 32; i++) {
		void __iomem *base = irq_base(i);

		__raw_writel(saved_icmr[i], base + ICMR);
		__raw_writel(0, base + ICLR);
	}

	if (cpu_has_ipr())
		for (i = 0; i < pxa_internal_irq_nr; i++)
			__raw_writel(saved_ipr[i], IRQ_BASE + IPR(i));

	__raw_writel(1, IRQ_BASE + ICCR);
	return 0;
}
#else
#define pxa_irq_suspend		NULL
#define pxa_irq_resume		NULL
#endif

struct sysdev_class pxa_irq_sysclass = {
	.name		= "irq",
	.suspend	= pxa_irq_suspend,
	.resume		= pxa_irq_resume,
};

static int __init pxa_irq_init(void)
{
	return sysdev_class_register(&pxa_irq_sysclass);
}

core_initcall(pxa_irq_init);
