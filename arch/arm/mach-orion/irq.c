/*
 * arch/arm/mach-orion/irq.c
 *
 * Core IRQ functions for Marvell Orion System On Chip
 *
 * Maintainer: Tzachi Perelstein <tzachi@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/gpio.h>
#include <asm/arch/orion.h>
#include "common.h"

/*****************************************************************************
 * Orion GPIO IRQ
 ****************************************************************************/
static void orion_gpio_irq_mask(u32 irq)
{
	int pin = irq_to_gpio(irq);
	orion_clrbits(GPIO_LEVEL_MASK, 1 << pin);
}

static void orion_gpio_irq_unmask(u32 irq)
{
	int pin = irq_to_gpio(irq);
	orion_setbits(GPIO_LEVEL_MASK, 1 << pin);
}

static int orion_gpio_set_irq_type(u32 irq, u32 type)
{
	int pin = irq_to_gpio(irq);

	if ((orion_read(GPIO_IO_CONF) & (1 << pin)) == 0) {
		printk(KERN_ERR "orion_gpio_set_irq_type failed "
				"(irq %d, pin %d).\n", irq, pin);
		return -EINVAL;
	}

	switch (type) {
	case IRQT_HIGH:
		orion_clrbits(GPIO_IN_POL, (1 << pin));
		break;
	case IRQT_LOW:
		orion_setbits(GPIO_IN_POL, (1 << pin));
		break;
	default:
		printk(KERN_ERR "failed to set irq=%d (type=%d)\n", irq, type);
		return -EINVAL;
	}

	return 0;
}

static struct irq_chip orion_gpio_irq_chip = {
	.name		= "Orion-IRQ-GPIO",
	.ack		= orion_gpio_irq_mask,
	.mask		= orion_gpio_irq_mask,
	.unmask		= orion_gpio_irq_unmask,
	.set_type	= orion_gpio_set_irq_type,
};

static void orion_gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	int i;
	u32 cause, shift;

	BUG_ON(irq < IRQ_ORION_GPIO_0_7 || irq > IRQ_ORION_GPIO_24_31);
	shift = (irq - IRQ_ORION_GPIO_0_7) * 8;
	cause = orion_read(GPIO_EDGE_CAUSE) & orion_read(GPIO_LEVEL_MASK);
	cause &= (0xff << shift);

	for (i = shift; i < shift + 8; i++) {
		if (cause & (1 << i)) {
			int gpio_irq = i + IRQ_ORION_GPIO_START;
			if (gpio_irq > 0) {
				desc = irq_desc + gpio_irq;
				desc_handle_irq(gpio_irq, desc);
			} else {
				printk(KERN_ERR "orion_gpio_irq_handler error, "
						"invalid irq %d\n", gpio_irq);
			}
		}
	}
}

static void __init orion_init_gpio_irq(void)
{
	int i;

	/*
	 * Mask and clear GPIO IRQ interrupts
	 */
	orion_write(GPIO_LEVEL_MASK, 0x0);
	orion_write(GPIO_EDGE_CAUSE, 0x0);

	/*
	 * Register chained level handlers for GPIO IRQs
	 */
	for (i = IRQ_ORION_GPIO_START; i < NR_IRQS; i++) {
		set_irq_chip(i, &orion_gpio_irq_chip);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID);
	}
	set_irq_chained_handler(IRQ_ORION_GPIO_0_7, orion_gpio_irq_handler);
	set_irq_chained_handler(IRQ_ORION_GPIO_8_15, orion_gpio_irq_handler);
	set_irq_chained_handler(IRQ_ORION_GPIO_16_23, orion_gpio_irq_handler);
	set_irq_chained_handler(IRQ_ORION_GPIO_24_31, orion_gpio_irq_handler);
}

/*****************************************************************************
 * Orion Main IRQ
 ****************************************************************************/
static void orion_main_irq_mask(u32 irq)
{
	orion_clrbits(MAIN_IRQ_MASK, 1 << irq);
}

static void orion_main_irq_unmask(u32 irq)
{
	orion_setbits(MAIN_IRQ_MASK, 1 << irq);
}

static struct irq_chip orion_main_irq_chip = {
	.name		= "Orion-IRQ-Main",
	.ack		= orion_main_irq_mask,
	.mask		= orion_main_irq_mask,
	.unmask		= orion_main_irq_unmask,
};

static void __init orion_init_main_irq(void)
{
	int i;

	/*
	 * Mask and clear Main IRQ interrupts
	 */
	orion_write(MAIN_IRQ_MASK, 0x0);
	orion_write(MAIN_IRQ_CAUSE, 0x0);

	/*
	 * Register level handler for Main IRQs
	 */
	for (i = 0; i < IRQ_ORION_GPIO_START; i++) {
		set_irq_chip(i, &orion_main_irq_chip);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID);
	}
}

void __init orion_init_irq(void)
{
	orion_init_main_irq();
	orion_init_gpio_irq();
}
