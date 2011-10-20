/* linux/arch/arm/mach-exynos4/irq-eint.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4 - IRQ EINT support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/sysdev.h>
#include <linux/gpio.h>

#include <plat/pm.h>
#include <plat/cpu.h>
#include <plat/gpio-cfg.h>

#include <mach/regs-gpio.h>

#include <asm/mach/irq.h>

static DEFINE_SPINLOCK(eint_lock);

static unsigned int eint0_15_data[16];

static unsigned int exynos4_get_irq_nr(unsigned int number)
{
	u32 ret = 0;

	switch (number) {
	case 0 ... 3:
		ret = (number + IRQ_EINT0);
		break;
	case 4 ... 7:
		ret = (number + (IRQ_EINT4 - 4));
		break;
	case 8 ... 15:
		ret = (number + (IRQ_EINT8 - 8));
		break;
	default:
		printk(KERN_ERR "number available : %d\n", number);
	}

	return ret;
}

static inline void exynos4_irq_eint_mask(struct irq_data *data)
{
	u32 mask;

	spin_lock(&eint_lock);
	mask = __raw_readl(S5P_EINT_MASK(EINT_REG_NR(data->irq)));
	mask |= eint_irq_to_bit(data->irq);
	__raw_writel(mask, S5P_EINT_MASK(EINT_REG_NR(data->irq)));
	spin_unlock(&eint_lock);
}

static void exynos4_irq_eint_unmask(struct irq_data *data)
{
	u32 mask;

	spin_lock(&eint_lock);
	mask = __raw_readl(S5P_EINT_MASK(EINT_REG_NR(data->irq)));
	mask &= ~(eint_irq_to_bit(data->irq));
	__raw_writel(mask, S5P_EINT_MASK(EINT_REG_NR(data->irq)));
	spin_unlock(&eint_lock);
}

static inline void exynos4_irq_eint_ack(struct irq_data *data)
{
	__raw_writel(eint_irq_to_bit(data->irq),
		     S5P_EINT_PEND(EINT_REG_NR(data->irq)));
}

static void exynos4_irq_eint_maskack(struct irq_data *data)
{
	exynos4_irq_eint_mask(data);
	exynos4_irq_eint_ack(data);
}

static int exynos4_irq_eint_set_type(struct irq_data *data, unsigned int type)
{
	int offs = EINT_OFFSET(data->irq);
	int shift;
	u32 ctrl, mask;
	u32 newvalue = 0;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		newvalue = S5P_IRQ_TYPE_EDGE_RISING;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		newvalue = S5P_IRQ_TYPE_EDGE_FALLING;
		break;

	case IRQ_TYPE_EDGE_BOTH:
		newvalue = S5P_IRQ_TYPE_EDGE_BOTH;
		break;

	case IRQ_TYPE_LEVEL_LOW:
		newvalue = S5P_IRQ_TYPE_LEVEL_LOW;
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		newvalue = S5P_IRQ_TYPE_LEVEL_HIGH;
		break;

	default:
		printk(KERN_ERR "No such irq type %d", type);
		return -EINVAL;
	}

	shift = (offs & 0x7) * 4;
	mask = 0x7 << shift;

	spin_lock(&eint_lock);
	ctrl = __raw_readl(S5P_EINT_CON(EINT_REG_NR(data->irq)));
	ctrl &= ~mask;
	ctrl |= newvalue << shift;
	__raw_writel(ctrl, S5P_EINT_CON(EINT_REG_NR(data->irq)));
	spin_unlock(&eint_lock);

	switch (offs) {
	case 0 ... 7:
		s3c_gpio_cfgpin(EINT_GPIO_0(offs & 0x7), EINT_MODE);
		break;
	case 8 ... 15:
		s3c_gpio_cfgpin(EINT_GPIO_1(offs & 0x7), EINT_MODE);
		break;
	case 16 ... 23:
		s3c_gpio_cfgpin(EINT_GPIO_2(offs & 0x7), EINT_MODE);
		break;
	case 24 ... 31:
		s3c_gpio_cfgpin(EINT_GPIO_3(offs & 0x7), EINT_MODE);
		break;
	default:
		printk(KERN_ERR "No such irq number %d", offs);
	}

	return 0;
}

static struct irq_chip exynos4_irq_eint = {
	.name		= "exynos4-eint",
	.irq_mask	= exynos4_irq_eint_mask,
	.irq_unmask	= exynos4_irq_eint_unmask,
	.irq_mask_ack	= exynos4_irq_eint_maskack,
	.irq_ack	= exynos4_irq_eint_ack,
	.irq_set_type	= exynos4_irq_eint_set_type,
#ifdef CONFIG_PM
	.irq_set_wake	= s3c_irqext_wake,
#endif
};

/* exynos4_irq_demux_eint
 *
 * This function demuxes the IRQ from from EINTs 16 to 31.
 * It is designed to be inlined into the specific handler
 * s5p_irq_demux_eintX_Y.
 *
 * Each EINT pend/mask registers handle eight of them.
 */
static inline void exynos4_irq_demux_eint(unsigned int start)
{
	unsigned int irq;

	u32 status = __raw_readl(S5P_EINT_PEND(EINT_REG_NR(start)));
	u32 mask = __raw_readl(S5P_EINT_MASK(EINT_REG_NR(start)));

	status &= ~mask;
	status &= 0xff;

	while (status) {
		irq = fls(status) - 1;
		generic_handle_irq(irq + start);
		status &= ~(1 << irq);
	}
}

static void exynos4_irq_demux_eint16_31(unsigned int irq, struct irq_desc *desc)
{
	struct irq_chip *chip = irq_get_chip(irq);
	chained_irq_enter(chip, desc);
	exynos4_irq_demux_eint(IRQ_EINT(16));
	exynos4_irq_demux_eint(IRQ_EINT(24));
	chained_irq_exit(chip, desc);
}

static void exynos4_irq_eint0_15(unsigned int irq, struct irq_desc *desc)
{
	u32 *irq_data = irq_get_handler_data(irq);
	struct irq_chip *chip = irq_get_chip(irq);

	chained_irq_enter(chip, desc);
	chip->irq_mask(&desc->irq_data);

	if (chip->irq_ack)
		chip->irq_ack(&desc->irq_data);

	generic_handle_irq(*irq_data);

	chip->irq_unmask(&desc->irq_data);
	chained_irq_exit(chip, desc);
}

int __init exynos4_init_irq_eint(void)
{
	int irq;

	for (irq = 0 ; irq <= 31 ; irq++) {
		irq_set_chip_and_handler(IRQ_EINT(irq), &exynos4_irq_eint,
					 handle_level_irq);
		set_irq_flags(IRQ_EINT(irq), IRQF_VALID);
	}

	irq_set_chained_handler(IRQ_EINT16_31, exynos4_irq_demux_eint16_31);

	for (irq = 0 ; irq <= 15 ; irq++) {
		eint0_15_data[irq] = IRQ_EINT(irq);

		irq_set_handler_data(exynos4_get_irq_nr(irq),
				     &eint0_15_data[irq]);
		irq_set_chained_handler(exynos4_get_irq_nr(irq),
					exynos4_irq_eint0_15);
	}

	return 0;
}

arch_initcall(exynos4_init_irq_eint);
