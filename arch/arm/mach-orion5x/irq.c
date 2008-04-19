/*
 * arch/arm/mach-orion5x/irq.c
 *
 * Core IRQ functions for Marvell Orion System On Chip
 *
 * Maintainer: Tzachi Perelstein <tzachi@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/gpio.h>
#include <asm/io.h>
#include <asm/arch/orion5x.h>
#include <asm/plat-orion/irq.h>
#include "common.h"

/*****************************************************************************
 * Orion GPIO IRQ
 *
 * GPIO_IN_POL register controlls whether GPIO_DATA_IN will hold the same
 * value of the line or the opposite value.
 *
 * Level IRQ handlers: DATA_IN is used directly as cause register.
 *                     Interrupt are masked by LEVEL_MASK registers.
 * Edge IRQ handlers:  Change in DATA_IN are latched in EDGE_CAUSE.
 *                     Interrupt are masked by EDGE_MASK registers.
 * Both-edge handlers: Similar to regular Edge handlers, but also swaps
 *                     the polarity to catch the next line transaction.
 *                     This is a race condition that might not perfectly
 *                     work on some use cases.
 *
 * Every eight GPIO lines are grouped (OR'ed) before going up to main
 * cause register.
 *
 *                    EDGE  cause    mask
 *        data-in   /--------| |-----| |----\
 *     -----| |-----                         ---- to main cause reg
 *           X      \----------------| |----/
 *        polarity    LEVEL          mask
 *
 ****************************************************************************/
static void orion5x_gpio_irq_ack(u32 irq)
{
	int pin = irq_to_gpio(irq);
	if (irq_desc[irq].status & IRQ_LEVEL)
		/*
		 * Mask bit for level interrupt
		 */
		orion5x_clrbits(GPIO_LEVEL_MASK, 1 << pin);
	else
		/*
		 * Clear casue bit for egde interrupt
		 */
		orion5x_clrbits(GPIO_EDGE_CAUSE, 1 << pin);
}

static void orion5x_gpio_irq_mask(u32 irq)
{
	int pin = irq_to_gpio(irq);
	if (irq_desc[irq].status & IRQ_LEVEL)
		orion5x_clrbits(GPIO_LEVEL_MASK, 1 << pin);
	else
		orion5x_clrbits(GPIO_EDGE_MASK, 1 << pin);
}

static void orion5x_gpio_irq_unmask(u32 irq)
{
	int pin = irq_to_gpio(irq);
	if (irq_desc[irq].status & IRQ_LEVEL)
		orion5x_setbits(GPIO_LEVEL_MASK, 1 << pin);
	else
		orion5x_setbits(GPIO_EDGE_MASK, 1 << pin);
}

static int orion5x_gpio_set_irq_type(u32 irq, u32 type)
{
	int pin = irq_to_gpio(irq);
	struct irq_desc *desc;

	if ((orion5x_read(GPIO_IO_CONF) & (1 << pin)) == 0) {
		printk(KERN_ERR "orion5x_gpio_set_irq_type failed "
				"(irq %d, pin %d).\n", irq, pin);
		return -EINVAL;
	}

	desc = irq_desc + irq;

	switch (type) {
	case IRQT_HIGH:
		desc->handle_irq = handle_level_irq;
		desc->status |= IRQ_LEVEL;
		orion5x_clrbits(GPIO_IN_POL, (1 << pin));
		break;
	case IRQT_LOW:
		desc->handle_irq = handle_level_irq;
		desc->status |= IRQ_LEVEL;
		orion5x_setbits(GPIO_IN_POL, (1 << pin));
		break;
	case IRQT_RISING:
		desc->handle_irq = handle_edge_irq;
		desc->status &= ~IRQ_LEVEL;
		orion5x_clrbits(GPIO_IN_POL, (1 << pin));
		break;
	case IRQT_FALLING:
		desc->handle_irq = handle_edge_irq;
		desc->status &= ~IRQ_LEVEL;
		orion5x_setbits(GPIO_IN_POL, (1 << pin));
		break;
	case IRQT_BOTHEDGE:
		desc->handle_irq = handle_edge_irq;
		desc->status &= ~IRQ_LEVEL;
		/*
		 * set initial polarity based on current input level
		 */
		if ((orion5x_read(GPIO_IN_POL) ^ orion5x_read(GPIO_DATA_IN))
		    & (1 << pin))
			orion5x_setbits(GPIO_IN_POL, (1 << pin)); /* falling */
		else
			orion5x_clrbits(GPIO_IN_POL, (1 << pin)); /* rising */

		break;
	default:
		printk(KERN_ERR "failed to set irq=%d (type=%d)\n", irq, type);
		return -EINVAL;
	}

	desc->status &= ~IRQ_TYPE_SENSE_MASK;
	desc->status |= type & IRQ_TYPE_SENSE_MASK;

	return 0;
}

static struct irq_chip orion5x_gpio_irq_chip = {
	.name		= "Orion-IRQ-GPIO",
	.ack		= orion5x_gpio_irq_ack,
	.mask		= orion5x_gpio_irq_mask,
	.unmask		= orion5x_gpio_irq_unmask,
	.set_type	= orion5x_gpio_set_irq_type,
};

static void orion5x_gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	u32 cause, offs, pin;

	BUG_ON(irq < IRQ_ORION5X_GPIO_0_7 || irq > IRQ_ORION5X_GPIO_24_31);
	offs = (irq - IRQ_ORION5X_GPIO_0_7) * 8;
	cause = (orion5x_read(GPIO_DATA_IN) & orion5x_read(GPIO_LEVEL_MASK)) |
		(orion5x_read(GPIO_EDGE_CAUSE) & orion5x_read(GPIO_EDGE_MASK));

	for (pin = offs; pin < offs + 8; pin++) {
		if (cause & (1 << pin)) {
			irq = gpio_to_irq(pin);
			desc = irq_desc + irq;
			if ((desc->status & IRQ_TYPE_SENSE_MASK) == IRQT_BOTHEDGE) {
				/* Swap polarity (race with GPIO line) */
				u32 polarity = orion5x_read(GPIO_IN_POL);
				polarity ^= 1 << pin;
				orion5x_write(GPIO_IN_POL, polarity);
			}
			desc_handle_irq(irq, desc);
		}
	}
}

static void __init orion5x_init_gpio_irq(void)
{
	int i;
	struct irq_desc *desc;

	/*
	 * Mask and clear GPIO IRQ interrupts
	 */
	orion5x_write(GPIO_LEVEL_MASK, 0x0);
	orion5x_write(GPIO_EDGE_MASK, 0x0);
	orion5x_write(GPIO_EDGE_CAUSE, 0x0);

	/*
	 * Register chained level handlers for GPIO IRQs by default.
	 * User can use set_type() if he wants to use edge types handlers.
	 */
	for (i = IRQ_ORION5X_GPIO_START; i < NR_IRQS; i++) {
		set_irq_chip(i, &orion5x_gpio_irq_chip);
		set_irq_handler(i, handle_level_irq);
		desc = irq_desc + i;
		desc->status |= IRQ_LEVEL;
		set_irq_flags(i, IRQF_VALID);
	}
	set_irq_chained_handler(IRQ_ORION5X_GPIO_0_7, orion5x_gpio_irq_handler);
	set_irq_chained_handler(IRQ_ORION5X_GPIO_8_15, orion5x_gpio_irq_handler);
	set_irq_chained_handler(IRQ_ORION5X_GPIO_16_23, orion5x_gpio_irq_handler);
	set_irq_chained_handler(IRQ_ORION5X_GPIO_24_31, orion5x_gpio_irq_handler);
}

/*****************************************************************************
 * Orion Main IRQ
 ****************************************************************************/
static void __init orion5x_init_main_irq(void)
{
	orion_irq_init(0, (void __iomem *)MAIN_IRQ_MASK);
}

void __init orion5x_init_irq(void)
{
	orion5x_init_main_irq();
	orion5x_init_gpio_irq();
}
