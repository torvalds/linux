/*
 * GPIO driver for Marvell SoCs
 *
 * Copyright (C) 2012 Marvell
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 * Andrew Lunn <andrew@lunn.ch>
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * This driver is a fairly straightforward GPIO driver for the
 * complete family of Marvell EBU SoC platforms (Orion, Dove,
 * Kirkwood, Discovery, Armada 370/XP). The only complexity of this
 * driver is the different register layout that exists between the
 * non-SMP platforms (Orion, Dove, Kirkwood, Armada 370) and the SMP
 * platforms (MV78200 from the Discovery family and the Armada
 * XP). Therefore, this driver handles three variants of the GPIO
 * block:
 * - the basic variant, called "orion-gpio", with the simplest
 *   register set. Used on Orion, Dove, Kirkwoord, Armada 370 and
 *   non-SMP Discovery systems
 * - the mv78200 variant for MV78200 Discovery systems. This variant
 *   turns the edge mask and level mask registers into CPU0 edge
 *   mask/level mask registers, and adds CPU1 edge mask/level mask
 *   registers.
 * - the armadaxp variant for Armada XP systems. This variant keeps
 *   the normal cause/edge mask/level mask registers when the global
 *   interrupts are used, but adds per-CPU cause/edge mask/level mask
 *   registers n a separate memory area for the per-CPU GPIO
 *   interrupts.
 */

#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/irqdomain.h>
#include <linux/io.h>
#include <linux/of_irq.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>

/*
 * GPIO unit register offsets.
 */
#define GPIO_OUT_OFF		0x0000
#define GPIO_IO_CONF_OFF	0x0004
#define GPIO_BLINK_EN_OFF	0x0008
#define GPIO_IN_POL_OFF		0x000c
#define GPIO_DATA_IN_OFF	0x0010
#define GPIO_EDGE_CAUSE_OFF	0x0014
#define GPIO_EDGE_MASK_OFF	0x0018
#define GPIO_LEVEL_MASK_OFF	0x001c

/* The MV78200 has per-CPU registers for edge mask and level mask */
#define GPIO_EDGE_MASK_MV78200_OFF(cpu)   ((cpu) ? 0x30 : 0x18)
#define GPIO_LEVEL_MASK_MV78200_OFF(cpu)  ((cpu) ? 0x34 : 0x1C)

/* The Armada XP has per-CPU registers for interrupt cause, interrupt
 * mask and interrupt level mask. Those are relative to the
 * percpu_membase. */
#define GPIO_EDGE_CAUSE_ARMADAXP_OFF(cpu) ((cpu) * 0x4)
#define GPIO_EDGE_MASK_ARMADAXP_OFF(cpu)  (0x10 + (cpu) * 0x4)
#define GPIO_LEVEL_MASK_ARMADAXP_OFF(cpu) (0x20 + (cpu) * 0x4)

#define MVEBU_GPIO_SOC_VARIANT_ORION    0x1
#define MVEBU_GPIO_SOC_VARIANT_MV78200  0x2
#define MVEBU_GPIO_SOC_VARIANT_ARMADAXP 0x3

#define MVEBU_MAX_GPIO_PER_BANK         32

struct mvebu_gpio_chip {
	struct gpio_chip   chip;
	spinlock_t	   lock;
	void __iomem	  *membase;
	void __iomem	  *percpu_membase;
	unsigned int       irqbase;
	struct irq_domain *domain;
	int                soc_variant;
};

/*
 * Functions returning addresses of individual registers for a given
 * GPIO controller.
 */
static inline void __iomem *mvebu_gpioreg_out(struct mvebu_gpio_chip *mvchip)
{
	return mvchip->membase + GPIO_OUT_OFF;
}

static inline void __iomem *mvebu_gpioreg_io_conf(struct mvebu_gpio_chip *mvchip)
{
	return mvchip->membase + GPIO_IO_CONF_OFF;
}

static inline void __iomem *mvebu_gpioreg_in_pol(struct mvebu_gpio_chip *mvchip)
{
	return mvchip->membase + GPIO_IN_POL_OFF;
}

static inline void __iomem *mvebu_gpioreg_data_in(struct mvebu_gpio_chip *mvchip)
{
	return mvchip->membase + GPIO_DATA_IN_OFF;
}

static inline void __iomem *mvebu_gpioreg_edge_cause(struct mvebu_gpio_chip *mvchip)
{
	int cpu;

	switch(mvchip->soc_variant) {
	case MVEBU_GPIO_SOC_VARIANT_ORION:
	case MVEBU_GPIO_SOC_VARIANT_MV78200:
		return mvchip->membase + GPIO_EDGE_CAUSE_OFF;
	case MVEBU_GPIO_SOC_VARIANT_ARMADAXP:
		cpu = smp_processor_id();
		return mvchip->percpu_membase + GPIO_EDGE_CAUSE_ARMADAXP_OFF(cpu);
	default:
		BUG();
	}
}

static inline void __iomem *mvebu_gpioreg_edge_mask(struct mvebu_gpio_chip *mvchip)
{
	int cpu;

	switch(mvchip->soc_variant) {
	case MVEBU_GPIO_SOC_VARIANT_ORION:
		return mvchip->membase + GPIO_EDGE_MASK_OFF;
	case MVEBU_GPIO_SOC_VARIANT_MV78200:
		cpu = smp_processor_id();
		return mvchip->membase + GPIO_EDGE_MASK_MV78200_OFF(cpu);
	case MVEBU_GPIO_SOC_VARIANT_ARMADAXP:
		cpu = smp_processor_id();
		return mvchip->percpu_membase + GPIO_EDGE_MASK_ARMADAXP_OFF(cpu);
	default:
		BUG();
	}
}

static void __iomem *mvebu_gpioreg_level_mask(struct mvebu_gpio_chip *mvchip)
{
	int cpu;

	switch(mvchip->soc_variant) {
	case MVEBU_GPIO_SOC_VARIANT_ORION:
		return mvchip->membase + GPIO_LEVEL_MASK_OFF;
	case MVEBU_GPIO_SOC_VARIANT_MV78200:
		cpu = smp_processor_id();
		return mvchip->membase + GPIO_LEVEL_MASK_MV78200_OFF(cpu);
	case MVEBU_GPIO_SOC_VARIANT_ARMADAXP:
		cpu = smp_processor_id();
		return mvchip->percpu_membase + GPIO_LEVEL_MASK_ARMADAXP_OFF(cpu);
	default:
		BUG();
	}
}

/*
 * Functions implementing the gpio_chip methods
 */

int mvebu_gpio_request(struct gpio_chip *chip, unsigned pin)
{
	return pinctrl_request_gpio(chip->base + pin);
}

void mvebu_gpio_free(struct gpio_chip *chip, unsigned pin)
{
	pinctrl_free_gpio(chip->base + pin);
}

static void mvebu_gpio_set(struct gpio_chip *chip, unsigned pin, int value)
{
	struct mvebu_gpio_chip *mvchip =
		container_of(chip, struct mvebu_gpio_chip, chip);
	unsigned long flags;
	u32 u;

	spin_lock_irqsave(&mvchip->lock, flags);
	u = readl_relaxed(mvebu_gpioreg_out(mvchip));
	if (value)
		u |= 1 << pin;
	else
		u &= ~(1 << pin);
	writel_relaxed(u, mvebu_gpioreg_out(mvchip));
	spin_unlock_irqrestore(&mvchip->lock, flags);
}

static int mvebu_gpio_get(struct gpio_chip *chip, unsigned pin)
{
	struct mvebu_gpio_chip *mvchip =
		container_of(chip, struct mvebu_gpio_chip, chip);
	u32 u;

	if (readl_relaxed(mvebu_gpioreg_io_conf(mvchip)) & (1 << pin)) {
		u = readl_relaxed(mvebu_gpioreg_data_in(mvchip)) ^
			readl_relaxed(mvebu_gpioreg_in_pol(mvchip));
	} else {
		u = readl_relaxed(mvebu_gpioreg_out(mvchip));
	}

	return (u >> pin) & 1;
}

static int mvebu_gpio_direction_input(struct gpio_chip *chip, unsigned pin)
{
	struct mvebu_gpio_chip *mvchip =
		container_of(chip, struct mvebu_gpio_chip, chip);
	unsigned long flags;
	int ret;
	u32 u;

	/* Check with the pinctrl driver whether this pin is usable as
	 * an input GPIO */
	ret = pinctrl_gpio_direction_input(chip->base + pin);
	if (ret)
		return ret;

	spin_lock_irqsave(&mvchip->lock, flags);
	u = readl_relaxed(mvebu_gpioreg_io_conf(mvchip));
	u |= 1 << pin;
	writel_relaxed(u, mvebu_gpioreg_io_conf(mvchip));
	spin_unlock_irqrestore(&mvchip->lock, flags);

	return 0;
}

static int mvebu_gpio_direction_output(struct gpio_chip *chip, unsigned pin,
				       int value)
{
	struct mvebu_gpio_chip *mvchip =
		container_of(chip, struct mvebu_gpio_chip, chip);
	unsigned long flags;
	int ret;
	u32 u;

	/* Check with the pinctrl driver whether this pin is usable as
	 * an output GPIO */
	ret = pinctrl_gpio_direction_output(chip->base + pin);
	if (ret)
		return ret;

	spin_lock_irqsave(&mvchip->lock, flags);
	u = readl_relaxed(mvebu_gpioreg_io_conf(mvchip));
	u &= ~(1 << pin);
	writel_relaxed(u, mvebu_gpioreg_io_conf(mvchip));
	spin_unlock_irqrestore(&mvchip->lock, flags);

	return 0;
}

static int mvebu_gpio_to_irq(struct gpio_chip *chip, unsigned pin)
{
	struct mvebu_gpio_chip *mvchip =
		container_of(chip, struct mvebu_gpio_chip, chip);
	return irq_create_mapping(mvchip->domain, pin);
}

/*
 * Functions implementing the irq_chip methods
 */
static void mvebu_gpio_irq_ack(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct mvebu_gpio_chip *mvchip = gc->private;
	u32 mask = ~(1 << (d->irq - gc->irq_base));

	irq_gc_lock(gc);
	writel_relaxed(mask, mvebu_gpioreg_edge_cause(mvchip));
	irq_gc_unlock(gc);
}

static void mvebu_gpio_edge_irq_mask(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct mvebu_gpio_chip *mvchip = gc->private;
	u32 mask = 1 << (d->irq - gc->irq_base);

	irq_gc_lock(gc);
	gc->mask_cache &= ~mask;
	writel_relaxed(gc->mask_cache, mvebu_gpioreg_edge_mask(mvchip));
	irq_gc_unlock(gc);
}

static void mvebu_gpio_edge_irq_unmask(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct mvebu_gpio_chip *mvchip = gc->private;
	u32 mask = 1 << (d->irq - gc->irq_base);

	irq_gc_lock(gc);
	gc->mask_cache |= mask;
	writel_relaxed(gc->mask_cache, mvebu_gpioreg_edge_mask(mvchip));
	irq_gc_unlock(gc);
}

static void mvebu_gpio_level_irq_mask(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct mvebu_gpio_chip *mvchip = gc->private;
	u32 mask = 1 << (d->irq - gc->irq_base);

	irq_gc_lock(gc);
	gc->mask_cache &= ~mask;
	writel_relaxed(gc->mask_cache, mvebu_gpioreg_level_mask(mvchip));
	irq_gc_unlock(gc);
}

static void mvebu_gpio_level_irq_unmask(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct mvebu_gpio_chip *mvchip = gc->private;
	u32 mask = 1 << (d->irq - gc->irq_base);

	irq_gc_lock(gc);
	gc->mask_cache |= mask;
	writel_relaxed(gc->mask_cache, mvebu_gpioreg_level_mask(mvchip));
	irq_gc_unlock(gc);
}

/*****************************************************************************
 * MVEBU GPIO IRQ
 *
 * GPIO_IN_POL register controls whether GPIO_DATA_IN will hold the same
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

static int mvebu_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	struct mvebu_gpio_chip *mvchip = gc->private;
	int pin;
	u32 u;

	pin = d->hwirq;

	u = readl_relaxed(mvebu_gpioreg_io_conf(mvchip)) & (1 << pin);
	if (!u) {
		return -EINVAL;
	}

	type &= IRQ_TYPE_SENSE_MASK;
	if (type == IRQ_TYPE_NONE)
		return -EINVAL;

	/* Check if we need to change chip and handler */
	if (!(ct->type & type))
		if (irq_setup_alt_chip(d, type))
			return -EINVAL;

	/*
	 * Configure interrupt polarity.
	 */
	switch(type) {
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_LEVEL_HIGH:
		u = readl_relaxed(mvebu_gpioreg_in_pol(mvchip));
		u &= ~(1 << pin);
		writel_relaxed(u, mvebu_gpioreg_in_pol(mvchip));
	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_LEVEL_LOW:
		u = readl_relaxed(mvebu_gpioreg_in_pol(mvchip));
		u |= 1 << pin;
		writel_relaxed(u, mvebu_gpioreg_in_pol(mvchip));
	case IRQ_TYPE_EDGE_BOTH: {
		u32 v;

		v = readl_relaxed(mvebu_gpioreg_in_pol(mvchip)) ^
			readl_relaxed(mvebu_gpioreg_data_in(mvchip));

		/*
		 * set initial polarity based on current input level
		 */
		u = readl_relaxed(mvebu_gpioreg_in_pol(mvchip));
		if (v & (1 << pin))
			u |= 1 << pin;		/* falling */
		else
			u &= ~(1 << pin);	/* rising */
		writel_relaxed(u, mvebu_gpioreg_in_pol(mvchip));
	}
	}
	return 0;
}

static void mvebu_gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct mvebu_gpio_chip *mvchip = irq_get_handler_data(irq);
	u32 cause, type;
	int i;

	if (mvchip == NULL)
		return;

	cause = readl_relaxed(mvebu_gpioreg_data_in(mvchip)) &
		readl_relaxed(mvebu_gpioreg_level_mask(mvchip));
	cause |= readl_relaxed(mvebu_gpioreg_edge_cause(mvchip)) &
		readl_relaxed(mvebu_gpioreg_edge_mask(mvchip));

	for (i = 0; i < mvchip->chip.ngpio; i++) {
		int irq;

		irq = mvchip->irqbase + i;

		if (!(cause & (1 << i)))
			continue;

		type = irqd_get_trigger_type(irq_get_irq_data(irq));
		if ((type & IRQ_TYPE_SENSE_MASK) == IRQ_TYPE_EDGE_BOTH) {
			/* Swap polarity (race with GPIO line) */
			u32 polarity;

			polarity = readl_relaxed(mvebu_gpioreg_in_pol(mvchip));
			polarity ^= 1 << i;
			writel_relaxed(polarity, mvebu_gpioreg_in_pol(mvchip));
		}
		generic_handle_irq(irq);
	}
}

static struct platform_device_id mvebu_gpio_ids[] = {
	{
		.name = "orion-gpio",
	}, {
		.name = "mv78200-gpio",
	}, {
		.name = "armadaxp-gpio",
	}, {
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(platform, mvebu_gpio_ids);

static struct of_device_id mvebu_gpio_of_match[] __devinitdata = {
	{
		.compatible = "marvell,orion-gpio",
		.data       = (void*) MVEBU_GPIO_SOC_VARIANT_ORION,
	},
	{
		.compatible = "marvell,mv78200-gpio",
		.data       = (void*) MVEBU_GPIO_SOC_VARIANT_MV78200,
	},
	{
		.compatible = "marvell,armadaxp-gpio",
		.data       = (void*) MVEBU_GPIO_SOC_VARIANT_ARMADAXP,
	},
	{
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(of, mvebu_gpio_of_match);

static int __devinit mvebu_gpio_probe(struct platform_device *pdev)
{
	struct mvebu_gpio_chip *mvchip;
	const struct of_device_id *match;
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;
	unsigned int ngpios;
	int soc_variant;
	int i, cpu, id;

	match = of_match_device(mvebu_gpio_of_match, &pdev->dev);
	if (match)
		soc_variant = (int) match->data;
	else
		soc_variant = MVEBU_GPIO_SOC_VARIANT_ORION;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (! res) {
		dev_err(&pdev->dev, "Cannot get memory resource\n");
		return -ENODEV;
	}

	mvchip = devm_kzalloc(&pdev->dev, sizeof(struct mvebu_gpio_chip), GFP_KERNEL);
	if (! mvchip){
		dev_err(&pdev->dev, "Cannot allocate memory\n");
		return -ENOMEM;
	}

	if (of_property_read_u32(pdev->dev.of_node, "ngpios", &ngpios)) {
		dev_err(&pdev->dev, "Missing ngpios OF property\n");
		return -ENODEV;
	}

	id = of_alias_get_id(pdev->dev.of_node, "gpio");
	if (id < 0) {
		dev_err(&pdev->dev, "Couldn't get OF id\n");
		return id;
	}

	mvchip->soc_variant = soc_variant;
	mvchip->chip.label = dev_name(&pdev->dev);
	mvchip->chip.dev = &pdev->dev;
	mvchip->chip.request = mvebu_gpio_request;
	mvchip->chip.direction_input = mvebu_gpio_direction_input;
	mvchip->chip.get = mvebu_gpio_get;
	mvchip->chip.direction_output = mvebu_gpio_direction_output;
	mvchip->chip.set = mvebu_gpio_set;
	mvchip->chip.to_irq = mvebu_gpio_to_irq;
	mvchip->chip.base = id * MVEBU_MAX_GPIO_PER_BANK;
	mvchip->chip.ngpio = ngpios;
	mvchip->chip.can_sleep = 0;
#ifdef CONFIG_OF
	mvchip->chip.of_node = np;
#endif

	spin_lock_init(&mvchip->lock);
	mvchip->membase = devm_request_and_ioremap(&pdev->dev, res);
	if (! mvchip->membase) {
		dev_err(&pdev->dev, "Cannot ioremap\n");
		kfree(mvchip->chip.label);
		return -ENOMEM;
	}

	/* The Armada XP has a second range of registers for the
	 * per-CPU registers */
	if (soc_variant == MVEBU_GPIO_SOC_VARIANT_ARMADAXP) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		if (! res) {
			dev_err(&pdev->dev, "Cannot get memory resource\n");
			kfree(mvchip->chip.label);
			return -ENODEV;
		}

		mvchip->percpu_membase = devm_request_and_ioremap(&pdev->dev, res);
		if (! mvchip->percpu_membase) {
			dev_err(&pdev->dev, "Cannot ioremap\n");
			kfree(mvchip->chip.label);
			return -ENOMEM;
		}
	}

	/*
	 * Mask and clear GPIO interrupts.
	 */
	switch(soc_variant) {
	case MVEBU_GPIO_SOC_VARIANT_ORION:
		writel_relaxed(0, mvchip->membase + GPIO_EDGE_CAUSE_OFF);
		writel_relaxed(0, mvchip->membase + GPIO_EDGE_MASK_OFF);
		writel_relaxed(0, mvchip->membase + GPIO_LEVEL_MASK_OFF);
		break;
	case MVEBU_GPIO_SOC_VARIANT_MV78200:
		writel_relaxed(0, mvchip->membase + GPIO_EDGE_CAUSE_OFF);
		for (cpu = 0; cpu < 2; cpu++) {
			writel_relaxed(0, mvchip->membase +
				       GPIO_EDGE_MASK_MV78200_OFF(cpu));
			writel_relaxed(0, mvchip->membase +
				       GPIO_LEVEL_MASK_MV78200_OFF(cpu));
		}
		break;
	case MVEBU_GPIO_SOC_VARIANT_ARMADAXP:
		writel_relaxed(0, mvchip->membase + GPIO_EDGE_CAUSE_OFF);
		writel_relaxed(0, mvchip->membase + GPIO_EDGE_MASK_OFF);
		writel_relaxed(0, mvchip->membase + GPIO_LEVEL_MASK_OFF);
		for (cpu = 0; cpu < 4; cpu++) {
			writel_relaxed(0, mvchip->percpu_membase +
				       GPIO_EDGE_CAUSE_ARMADAXP_OFF(cpu));
			writel_relaxed(0, mvchip->percpu_membase +
				       GPIO_EDGE_MASK_ARMADAXP_OFF(cpu));
			writel_relaxed(0, mvchip->percpu_membase +
				       GPIO_LEVEL_MASK_ARMADAXP_OFF(cpu));
		}
		break;
	default:
		BUG();
	}

	gpiochip_add(&mvchip->chip);

	/* Some gpio controllers do not provide irq support */
	if (!of_irq_count(np))
		return 0;

	/* Setup the interrupt handlers. Each chip can have up to 4
	 * interrupt handlers, with each handler dealing with 8 GPIO
	 * pins. */
	for (i = 0; i < 4; i++) {
		int irq;
		irq = platform_get_irq(pdev, i);
		if (irq < 0)
			continue;
		irq_set_handler_data(irq, mvchip);
		irq_set_chained_handler(irq, mvebu_gpio_irq_handler);
	}

	mvchip->irqbase = irq_alloc_descs(-1, 0, ngpios, -1);
	if (mvchip->irqbase < 0) {
		dev_err(&pdev->dev, "no irqs\n");
		kfree(mvchip->chip.label);
		return -ENOMEM;
	}

	gc = irq_alloc_generic_chip("mvebu_gpio_irq", 2, mvchip->irqbase,
				    mvchip->membase, handle_level_irq);
	if (! gc) {
		dev_err(&pdev->dev, "Cannot allocate generic irq_chip\n");
		kfree(mvchip->chip.label);
		return -ENOMEM;
	}

	gc->private = mvchip;
	ct = &gc->chip_types[0];
	ct->type = IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW;
	ct->chip.irq_mask = mvebu_gpio_level_irq_mask;
	ct->chip.irq_unmask = mvebu_gpio_level_irq_unmask;
	ct->chip.irq_set_type = mvebu_gpio_irq_set_type;
	ct->chip.name = mvchip->chip.label;

	ct = &gc->chip_types[1];
	ct->type = IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING;
	ct->chip.irq_ack = mvebu_gpio_irq_ack;
	ct->chip.irq_mask = mvebu_gpio_edge_irq_mask;
	ct->chip.irq_unmask = mvebu_gpio_edge_irq_unmask;
	ct->chip.irq_set_type = mvebu_gpio_irq_set_type;
	ct->handler = handle_edge_irq;
	ct->chip.name = mvchip->chip.label;

	irq_setup_generic_chip(gc, IRQ_MSK(ngpios), IRQ_GC_INIT_MASK_CACHE,
			       IRQ_NOREQUEST, IRQ_LEVEL | IRQ_NOPROBE);

	/* Setup irq domain on top of the generic chip. */
	mvchip->domain = irq_domain_add_legacy(np, mvchip->chip.ngpio,
					       mvchip->irqbase, 0,
					       &irq_domain_simple_ops,
					       mvchip);
	if (!mvchip->domain) {
		dev_err(&pdev->dev, "couldn't allocate irq domain %s (DT).\n",
			mvchip->chip.label);
		irq_remove_generic_chip(gc, IRQ_MSK(ngpios), IRQ_NOREQUEST,
					IRQ_LEVEL | IRQ_NOPROBE);
		kfree(gc);
		kfree(mvchip->chip.label);
		return -ENODEV;
	}

	return 0;
}

static struct platform_driver mvebu_gpio_driver = {
	.driver		= {
		.name	        = "mvebu-gpio",
		.owner	        = THIS_MODULE,
		.of_match_table = mvebu_gpio_of_match,
	},
	.probe		= mvebu_gpio_probe,
	.id_table	= mvebu_gpio_ids,
};

static int __init mvebu_gpio_init(void)
{
	return platform_driver_register(&mvebu_gpio_driver);
}
postcore_initcall(mvebu_gpio_init);
