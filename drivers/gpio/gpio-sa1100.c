/*
 * linux/arch/arm/mach-sa1100/gpio.c
 *
 * Generic SA-1100 GPIO handling
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/syscore_ops.h>
#include <soc/sa1100/pwer.h>
#include <mach/hardware.h>
#include <mach/irqs.h>

struct sa1100_gpio_chip {
	struct gpio_chip chip;
	void __iomem *membase;
	int irqbase;
	u32 irqmask;
	u32 irqrising;
	u32 irqfalling;
	u32 irqwake;
};

#define sa1100_gpio_chip(x) container_of(x, struct sa1100_gpio_chip, chip)

enum {
	R_GPLR = 0x00,
	R_GPDR = 0x04,
	R_GPSR = 0x08,
	R_GPCR = 0x0c,
	R_GRER = 0x10,
	R_GFER = 0x14,
	R_GEDR = 0x18,
	R_GAFR = 0x1c,
};

static int sa1100_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	return readl_relaxed(sa1100_gpio_chip(chip)->membase + R_GPLR) &
		BIT(offset);
}

static void sa1100_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	int reg = value ? R_GPSR : R_GPCR;

	writel_relaxed(BIT(offset), sa1100_gpio_chip(chip)->membase + reg);
}

static int sa1100_get_direction(struct gpio_chip *chip, unsigned offset)
{
	void __iomem *gpdr = sa1100_gpio_chip(chip)->membase + R_GPDR;

	return !(readl_relaxed(gpdr) & BIT(offset));
}

static int sa1100_direction_input(struct gpio_chip *chip, unsigned offset)
{
	void __iomem *gpdr = sa1100_gpio_chip(chip)->membase + R_GPDR;
	unsigned long flags;

	local_irq_save(flags);
	writel_relaxed(readl_relaxed(gpdr) & ~BIT(offset), gpdr);
	local_irq_restore(flags);

	return 0;
}

static int sa1100_direction_output(struct gpio_chip *chip, unsigned offset, int value)
{
	void __iomem *gpdr = sa1100_gpio_chip(chip)->membase + R_GPDR;
	unsigned long flags;

	local_irq_save(flags);
	sa1100_gpio_set(chip, offset, value);
	writel_relaxed(readl_relaxed(gpdr) | BIT(offset), gpdr);
	local_irq_restore(flags);

	return 0;
}

static int sa1100_to_irq(struct gpio_chip *chip, unsigned offset)
{
	return sa1100_gpio_chip(chip)->irqbase + offset;
}

static struct sa1100_gpio_chip sa1100_gpio_chip = {
	.chip = {
		.label			= "gpio",
		.get_direction		= sa1100_get_direction,
		.direction_input	= sa1100_direction_input,
		.direction_output	= sa1100_direction_output,
		.set			= sa1100_gpio_set,
		.get			= sa1100_gpio_get,
		.to_irq			= sa1100_to_irq,
		.base			= 0,
		.ngpio			= GPIO_MAX + 1,
	},
	.membase = (void *)&GPLR,
	.irqbase = IRQ_GPIO0,
};

/*
 * SA1100 GPIO edge detection for IRQs:
 * IRQs are generated on Falling-Edge, Rising-Edge, or both.
 * Use this instead of directly setting GRER/GFER.
 */
static void sa1100_update_edge_regs(struct sa1100_gpio_chip *sgc)
{
	void *base = sgc->membase;
	u32 grer, gfer;

	grer = sgc->irqrising & sgc->irqmask;
	gfer = sgc->irqfalling & sgc->irqmask;

	writel_relaxed(grer, base + R_GRER);
	writel_relaxed(gfer, base + R_GFER);
}

static int sa1100_gpio_type(struct irq_data *d, unsigned int type)
{
	struct sa1100_gpio_chip *sgc = irq_data_get_irq_chip_data(d);
	unsigned int mask = BIT(d->hwirq);

	if (type == IRQ_TYPE_PROBE) {
		if ((sgc->irqrising | sgc->irqfalling) & mask)
			return 0;
		type = IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING;
	}

	if (type & IRQ_TYPE_EDGE_RISING)
		sgc->irqrising |= mask;
	else
		sgc->irqrising &= ~mask;
	if (type & IRQ_TYPE_EDGE_FALLING)
		sgc->irqfalling |= mask;
	else
		sgc->irqfalling &= ~mask;

	sa1100_update_edge_regs(sgc);

	return 0;
}

/*
 * GPIO IRQs must be acknowledged.
 */
static void sa1100_gpio_ack(struct irq_data *d)
{
	struct sa1100_gpio_chip *sgc = irq_data_get_irq_chip_data(d);

	writel_relaxed(BIT(d->hwirq), sgc->membase + R_GEDR);
}

static void sa1100_gpio_mask(struct irq_data *d)
{
	struct sa1100_gpio_chip *sgc = irq_data_get_irq_chip_data(d);
	unsigned int mask = BIT(d->hwirq);

	sgc->irqmask &= ~mask;

	sa1100_update_edge_regs(sgc);
}

static void sa1100_gpio_unmask(struct irq_data *d)
{
	struct sa1100_gpio_chip *sgc = irq_data_get_irq_chip_data(d);
	unsigned int mask = BIT(d->hwirq);

	sgc->irqmask |= mask;

	sa1100_update_edge_regs(sgc);
}

static int sa1100_gpio_wake(struct irq_data *d, unsigned int on)
{
	struct sa1100_gpio_chip *sgc = irq_data_get_irq_chip_data(d);
	int ret = sa11x0_gpio_set_wake(d->hwirq, on);
	if (!ret) {
		if (on)
			sgc->irqwake |= BIT(d->hwirq);
		else
			sgc->irqwake &= ~BIT(d->hwirq);
	}
	return ret;
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
	struct sa1100_gpio_chip *sgc = d->host_data;

	irq_set_chip_data(irq, sgc);
	irq_set_chip_and_handler(irq, &sa1100_gpio_irq_chip, handle_edge_irq);
	irq_set_probe(irq);

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
static void sa1100_gpio_handler(struct irq_desc *desc)
{
	struct sa1100_gpio_chip *sgc = irq_desc_get_handler_data(desc);
	unsigned int irq, mask;
	void __iomem *gedr = sgc->membase + R_GEDR;

	mask = readl_relaxed(gedr);
	do {
		/*
		 * clear down all currently active IRQ sources.
		 * We will be processing them all.
		 */
		writel_relaxed(mask, gedr);

		irq = sgc->irqbase;
		do {
			if (mask & 1)
				generic_handle_irq(irq);
			mask >>= 1;
			irq++;
		} while (mask);

		mask = readl_relaxed(gedr);
	} while (mask);
}

static int sa1100_gpio_suspend(void)
{
	struct sa1100_gpio_chip *sgc = &sa1100_gpio_chip;

	/*
	 * Set the appropriate edges for wakeup.
	 */
	writel_relaxed(sgc->irqwake & sgc->irqrising, sgc->membase + R_GRER);
	writel_relaxed(sgc->irqwake & sgc->irqfalling, sgc->membase + R_GFER);

	/*
	 * Clear any pending GPIO interrupts.
	 */
	writel_relaxed(readl_relaxed(sgc->membase + R_GEDR),
		       sgc->membase + R_GEDR);

	return 0;
}

static void sa1100_gpio_resume(void)
{
	sa1100_update_edge_regs(&sa1100_gpio_chip);
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

static const int sa1100_gpio_irqs[] __initconst = {
	/* Install handlers for GPIO 0-10 edge detect interrupts */
	IRQ_GPIO0_SC,
	IRQ_GPIO1_SC,
	IRQ_GPIO2_SC,
	IRQ_GPIO3_SC,
	IRQ_GPIO4_SC,
	IRQ_GPIO5_SC,
	IRQ_GPIO6_SC,
	IRQ_GPIO7_SC,
	IRQ_GPIO8_SC,
	IRQ_GPIO9_SC,
	IRQ_GPIO10_SC,
	/* Install handler for GPIO 11-27 edge detect interrupts */
	IRQ_GPIO11_27,
};

void __init sa1100_init_gpio(void)
{
	struct sa1100_gpio_chip *sgc = &sa1100_gpio_chip;
	int i;

	/* clear all GPIO edge detects */
	writel_relaxed(0, sgc->membase + R_GFER);
	writel_relaxed(0, sgc->membase + R_GRER);
	writel_relaxed(-1, sgc->membase + R_GEDR);

	gpiochip_add_data(&sa1100_gpio_chip.chip, NULL);

	sa1100_gpio_irqdomain = irq_domain_add_simple(NULL,
			28, IRQ_GPIO0,
			&sa1100_gpio_irqdomain_ops, sgc);

	for (i = 0; i < ARRAY_SIZE(sa1100_gpio_irqs); i++)
		irq_set_chained_handler_and_data(sa1100_gpio_irqs[i],
						 sa1100_gpio_handler, sgc);
}
