/*
 * linux/arch/arm/mach-sa1100/gpio.c
 *
 * Generic SA-1100 GPIO handling
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/syscore_ops.h>
#include <mach/hardware.h>
#include <mach/irqs.h>

static int sa1100_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	return GPLR & GPIO_GPIO(offset);
}

static void sa1100_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	if (value)
		GPSR = GPIO_GPIO(offset);
	else
		GPCR = GPIO_GPIO(offset);
}

static int sa1100_direction_input(struct gpio_chip *chip, unsigned offset)
{
	unsigned long flags;

	local_irq_save(flags);
	GPDR &= ~GPIO_GPIO(offset);
	local_irq_restore(flags);
	return 0;
}

static int sa1100_direction_output(struct gpio_chip *chip, unsigned offset, int value)
{
	unsigned long flags;

	local_irq_save(flags);
	sa1100_gpio_set(chip, offset, value);
	GPDR |= GPIO_GPIO(offset);
	local_irq_restore(flags);
	return 0;
}

static int sa1100_to_irq(struct gpio_chip *chip, unsigned offset)
{
	return IRQ_GPIO0 + offset;
}

static struct gpio_chip sa1100_gpio_chip = {
	.label			= "gpio",
	.direction_input	= sa1100_direction_input,
	.direction_output	= sa1100_direction_output,
	.set			= sa1100_gpio_set,
	.get			= sa1100_gpio_get,
	.to_irq			= sa1100_to_irq,
	.base			= 0,
	.ngpio			= GPIO_MAX + 1,
};

/*
 * SA1100 GPIO edge detection for IRQs:
 * IRQs are generated on Falling-Edge, Rising-Edge, or both.
 * Use this instead of directly setting GRER/GFER.
 */
static int GPIO_IRQ_rising_edge;
static int GPIO_IRQ_falling_edge;
static int GPIO_IRQ_mask;

static int sa1100_gpio_type(struct irq_data *d, unsigned int type)
{
	unsigned int mask;

	mask = BIT(d->hwirq);

	if (type == IRQ_TYPE_PROBE) {
		if ((GPIO_IRQ_rising_edge | GPIO_IRQ_falling_edge) & mask)
			return 0;
		type = IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING;
	}

	if (type & IRQ_TYPE_EDGE_RISING)
		GPIO_IRQ_rising_edge |= mask;
	else
		GPIO_IRQ_rising_edge &= ~mask;
	if (type & IRQ_TYPE_EDGE_FALLING)
		GPIO_IRQ_falling_edge |= mask;
	else
		GPIO_IRQ_falling_edge &= ~mask;

	GRER = GPIO_IRQ_rising_edge & GPIO_IRQ_mask;
	GFER = GPIO_IRQ_falling_edge & GPIO_IRQ_mask;

	return 0;
}

/*
 * GPIO IRQs must be acknowledged.
 */
static void sa1100_gpio_ack(struct irq_data *d)
{
	GEDR = BIT(d->hwirq);
}

static void sa1100_gpio_mask(struct irq_data *d)
{
	unsigned int mask = BIT(d->hwirq);

	GPIO_IRQ_mask &= ~mask;

	GRER &= ~mask;
	GFER &= ~mask;
}

static void sa1100_gpio_unmask(struct irq_data *d)
{
	unsigned int mask = BIT(d->hwirq);

	GPIO_IRQ_mask |= mask;

	GRER = GPIO_IRQ_rising_edge & GPIO_IRQ_mask;
	GFER = GPIO_IRQ_falling_edge & GPIO_IRQ_mask;
}

static int sa1100_gpio_wake(struct irq_data *d, unsigned int on)
{
	if (on)
		PWER |= BIT(d->hwirq);
	else
		PWER &= ~BIT(d->hwirq);
	return 0;
}

/*
 * This is for GPIO IRQs
 */
static struct irq_chip sa1100_gpio_irq_chip = {
	.name		= "GPIO",
	.irq_ack	= sa1100_gpio_ack,
	.irq_mask	= sa1100_gpio_mask,
	.irq_unmask	= sa1100_gpio_unmask,
	.irq_set_type	= sa1100_gpio_type,
	.irq_set_wake	= sa1100_gpio_wake,
};

static int sa1100_gpio_irqdomain_map(struct irq_domain *d,
		unsigned int irq, irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &sa1100_gpio_irq_chip,
				 handle_edge_irq);
	set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);

	return 0;
}

static const struct irq_domain_ops sa1100_gpio_irqdomain_ops = {
	.map = sa1100_gpio_irqdomain_map,
	.xlate = irq_domain_xlate_onetwocell,
};

static struct irq_domain *sa1100_gpio_irqdomain;

/*
 * IRQ 0-11 (GPIO) handler.  We enter here with the
 * irq_controller_lock held, and IRQs disabled.  Decode the IRQ
 * and call the handler.
 */
static void
sa1100_gpio_handler(unsigned int __irq, struct irq_desc *desc)
{
	unsigned int irq, mask;

	mask = GEDR;
	do {
		/*
		 * clear down all currently active IRQ sources.
		 * We will be processing them all.
		 */
		GEDR = mask;

		irq = IRQ_GPIO0;
		do {
			if (mask & 1)
				generic_handle_irq(irq);
			mask >>= 1;
			irq++;
		} while (mask);

		mask = GEDR;
	} while (mask);
}

static int sa1100_gpio_suspend(void)
{
	/*
	 * Set the appropriate edges for wakeup.
	 */
	GRER = PWER & GPIO_IRQ_rising_edge;
	GFER = PWER & GPIO_IRQ_falling_edge;

	/*
	 * Clear any pending GPIO interrupts.
	 */
	GEDR = GEDR;

	return 0;
}

static void sa1100_gpio_resume(void)
{
	GRER = GPIO_IRQ_rising_edge & GPIO_IRQ_mask;
	GFER = GPIO_IRQ_falling_edge & GPIO_IRQ_mask;
}

static struct syscore_ops sa1100_gpio_syscore_ops = {
	.suspend	= sa1100_gpio_suspend,
	.resume		= sa1100_gpio_resume,
};

static int __init sa1100_gpio_init_devicefs(void)
{
	register_syscore_ops(&sa1100_gpio_syscore_ops);
	return 0;
}

device_initcall(sa1100_gpio_init_devicefs);

void __init sa1100_init_gpio(void)
{
	/* clear all GPIO edge detects */
	GFER = 0;
	GRER = 0;
	GEDR = -1;

	gpiochip_add(&sa1100_gpio_chip);

	sa1100_gpio_irqdomain = irq_domain_add_simple(NULL,
			28, IRQ_GPIO0,
			&sa1100_gpio_irqdomain_ops, NULL);

	/*
	 * Install handlers for GPIO 0-10 edge detect interrupts
	 */
	irq_set_chained_handler(IRQ_GPIO0_SC, sa1100_gpio_handler);
	irq_set_chained_handler(IRQ_GPIO1_SC, sa1100_gpio_handler);
	irq_set_chained_handler(IRQ_GPIO2_SC, sa1100_gpio_handler);
	irq_set_chained_handler(IRQ_GPIO3_SC, sa1100_gpio_handler);
	irq_set_chained_handler(IRQ_GPIO4_SC, sa1100_gpio_handler);
	irq_set_chained_handler(IRQ_GPIO5_SC, sa1100_gpio_handler);
	irq_set_chained_handler(IRQ_GPIO6_SC, sa1100_gpio_handler);
	irq_set_chained_handler(IRQ_GPIO7_SC, sa1100_gpio_handler);
	irq_set_chained_handler(IRQ_GPIO8_SC, sa1100_gpio_handler);
	irq_set_chained_handler(IRQ_GPIO9_SC, sa1100_gpio_handler);
	irq_set_chained_handler(IRQ_GPIO10_SC, sa1100_gpio_handler);
	/*
	 * Install handler for GPIO 11-27 edge detect interrupts
	 */
	irq_set_chained_handler(IRQ_GPIO11_27, sa1100_gpio_handler);

}
