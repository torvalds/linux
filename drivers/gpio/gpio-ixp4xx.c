// SPDX-License-Identifier: GPL-2.0
//
// IXP4 GPIO driver
// Copyright (C) 2019 Linus Walleij <linus.walleij@linaro.org>
//
// based on previous work and know-how from:
// Deepak Saxena <dsaxena@plexity.net>

#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/irqchip.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/bitops.h>
/* Include that go away with DT transition */
#include <linux/irqchip/irq-ixp4xx.h>

#include <asm/mach-types.h>

#define IXP4XX_REG_GPOUT	0x00
#define IXP4XX_REG_GPOE		0x04
#define IXP4XX_REG_GPIN		0x08
#define IXP4XX_REG_GPIS		0x0C
#define IXP4XX_REG_GPIT1	0x10
#define IXP4XX_REG_GPIT2	0x14
#define IXP4XX_REG_GPCLK	0x18
#define IXP4XX_REG_GPDBSEL	0x1C

/*
 * The hardware uses 3 bits to indicate interrupt "style".
 * we clear and set these three bits accordingly. The lower 24
 * bits in two registers (GPIT1 and GPIT2) are used to set up
 * the style for 8 lines each for a total of 16 GPIO lines.
 */
#define IXP4XX_GPIO_STYLE_ACTIVE_HIGH	0x0
#define IXP4XX_GPIO_STYLE_ACTIVE_LOW	0x1
#define IXP4XX_GPIO_STYLE_RISING_EDGE	0x2
#define IXP4XX_GPIO_STYLE_FALLING_EDGE	0x3
#define IXP4XX_GPIO_STYLE_TRANSITIONAL	0x4
#define IXP4XX_GPIO_STYLE_MASK		GENMASK(2, 0)
#define IXP4XX_GPIO_STYLE_SIZE		3

/**
 * struct ixp4xx_gpio - IXP4 GPIO state container
 * @dev: containing device for this instance
 * @fwnode: the fwnode for this GPIO chip
 * @gc: gpiochip for this instance
 * @domain: irqdomain for this chip instance
 * @base: remapped I/O-memory base
 * @irq_edge: Each bit represents an IRQ: 1: edge-triggered,
 * 0: level triggered
 */
struct ixp4xx_gpio {
	struct device *dev;
	struct fwnode_handle *fwnode;
	struct gpio_chip gc;
	struct irq_domain *domain;
	void __iomem *base;
	unsigned long long irq_edge;
};

/**
 * struct ixp4xx_gpio_map - IXP4 GPIO to parent IRQ map
 * @gpio_offset: offset of the IXP4 GPIO line
 * @parent_hwirq: hwirq on the parent IRQ controller
 */
struct ixp4xx_gpio_map {
	int gpio_offset;
	int parent_hwirq;
};

/* GPIO lines 0..12 have corresponding IRQs, GPIOs 13..15 have no IRQs */
const struct ixp4xx_gpio_map ixp4xx_gpiomap[] = {
	{ .gpio_offset = 0, .parent_hwirq = 6 },
	{ .gpio_offset = 1, .parent_hwirq = 7 },
	{ .gpio_offset = 2, .parent_hwirq = 19 },
	{ .gpio_offset = 3, .parent_hwirq = 20 },
	{ .gpio_offset = 4, .parent_hwirq = 21 },
	{ .gpio_offset = 5, .parent_hwirq = 22 },
	{ .gpio_offset = 6, .parent_hwirq = 23 },
	{ .gpio_offset = 7, .parent_hwirq = 24 },
	{ .gpio_offset = 8, .parent_hwirq = 25 },
	{ .gpio_offset = 9, .parent_hwirq = 26 },
	{ .gpio_offset = 10, .parent_hwirq = 27 },
	{ .gpio_offset = 11, .parent_hwirq = 28 },
	{ .gpio_offset = 12, .parent_hwirq = 29 },
};

static void ixp4xx_gpio_irq_ack(struct irq_data *d)
{
	struct ixp4xx_gpio *g = irq_data_get_irq_chip_data(d);

	__raw_writel(BIT(d->hwirq), g->base + IXP4XX_REG_GPIS);
}

static void ixp4xx_gpio_irq_unmask(struct irq_data *d)
{
	struct ixp4xx_gpio *g = irq_data_get_irq_chip_data(d);

	/* ACK when unmasking if not edge-triggered */
	if (!(g->irq_edge & BIT(d->hwirq)))
		ixp4xx_gpio_irq_ack(d);

	irq_chip_unmask_parent(d);
}

static int ixp4xx_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct ixp4xx_gpio *g = irq_data_get_irq_chip_data(d);
	int line = d->hwirq;
	unsigned long flags;
	u32 int_style;
	u32 int_reg;
	u32 val;

	switch (type) {
	case IRQ_TYPE_EDGE_BOTH:
		irq_set_handler_locked(d, handle_edge_irq);
		int_style = IXP4XX_GPIO_STYLE_TRANSITIONAL;
		g->irq_edge |= BIT(d->hwirq);
		break;
	case IRQ_TYPE_EDGE_RISING:
		irq_set_handler_locked(d, handle_edge_irq);
		int_style = IXP4XX_GPIO_STYLE_RISING_EDGE;
		g->irq_edge |= BIT(d->hwirq);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		irq_set_handler_locked(d, handle_edge_irq);
		int_style = IXP4XX_GPIO_STYLE_FALLING_EDGE;
		g->irq_edge |= BIT(d->hwirq);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		irq_set_handler_locked(d, handle_level_irq);
		int_style = IXP4XX_GPIO_STYLE_ACTIVE_HIGH;
		g->irq_edge &= ~BIT(d->hwirq);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		irq_set_handler_locked(d, handle_level_irq);
		int_style = IXP4XX_GPIO_STYLE_ACTIVE_LOW;
		g->irq_edge &= ~BIT(d->hwirq);
		break;
	default:
		return -EINVAL;
	}

	if (line >= 8) {
		/* pins 8-15 */
		line -= 8;
		int_reg = IXP4XX_REG_GPIT2;
	} else {
		/* pins 0-7 */
		int_reg = IXP4XX_REG_GPIT1;
	}

	spin_lock_irqsave(&g->gc.bgpio_lock, flags);

	/* Clear the style for the appropriate pin */
	val = __raw_readl(g->base + int_reg);
	val &= ~(IXP4XX_GPIO_STYLE_MASK << (line * IXP4XX_GPIO_STYLE_SIZE));
	__raw_writel(val, g->base + int_reg);

	__raw_writel(BIT(line), g->base + IXP4XX_REG_GPIS);

	/* Set the new style */
	val = __raw_readl(g->base + int_reg);
	val |= (int_style << (line * IXP4XX_GPIO_STYLE_SIZE));
	__raw_writel(val, g->base + int_reg);

	/* Force-configure this line as an input */
	val = __raw_readl(g->base + IXP4XX_REG_GPOE);
	val |= BIT(d->hwirq);
	__raw_writel(val, g->base + IXP4XX_REG_GPOE);

	spin_unlock_irqrestore(&g->gc.bgpio_lock, flags);

	/* This parent only accept level high (asserted) */
	return irq_chip_set_type_parent(d, IRQ_TYPE_LEVEL_HIGH);
}

static struct irq_chip ixp4xx_gpio_irqchip = {
	.name = "IXP4GPIO",
	.irq_ack = ixp4xx_gpio_irq_ack,
	.irq_mask = irq_chip_mask_parent,
	.irq_unmask = ixp4xx_gpio_irq_unmask,
	.irq_set_type = ixp4xx_gpio_irq_set_type,
};

static int ixp4xx_gpio_to_irq(struct gpio_chip *gc, unsigned int offset)
{
	struct ixp4xx_gpio *g = gpiochip_get_data(gc);
	struct irq_fwspec fwspec;

	fwspec.fwnode = g->fwnode;
	fwspec.param_count = 2;
	fwspec.param[0] = offset;
	fwspec.param[1] = IRQ_TYPE_NONE;

	return irq_create_fwspec_mapping(&fwspec);
}

static int ixp4xx_gpio_irq_domain_translate(struct irq_domain *domain,
					    struct irq_fwspec *fwspec,
					    unsigned long *hwirq,
					    unsigned int *type)
{
	int ret;

	/* We support standard DT translation */
	if (is_of_node(fwspec->fwnode) && fwspec->param_count == 2) {
		return irq_domain_translate_twocell(domain, fwspec,
						    hwirq, type);
	}

	/* This goes away when we transition to DT */
	if (is_fwnode_irqchip(fwspec->fwnode)) {
		ret = irq_domain_translate_twocell(domain, fwspec,
						   hwirq, type);
		if (ret)
			return ret;
		WARN_ON(*type == IRQ_TYPE_NONE);
		return 0;
	}
	return -EINVAL;
}

static int ixp4xx_gpio_irq_domain_alloc(struct irq_domain *d,
					unsigned int irq, unsigned int nr_irqs,
					void *data)
{
	struct ixp4xx_gpio *g = d->host_data;
	irq_hw_number_t hwirq;
	unsigned int type = IRQ_TYPE_NONE;
	struct irq_fwspec *fwspec = data;
	int ret;
	int i;

	ret = ixp4xx_gpio_irq_domain_translate(d, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	dev_dbg(g->dev, "allocate IRQ %d..%d, hwirq %lu..%lu\n",
		irq, irq + nr_irqs - 1,
		hwirq, hwirq + nr_irqs - 1);

	for (i = 0; i < nr_irqs; i++) {
		struct irq_fwspec parent_fwspec;
		const struct ixp4xx_gpio_map *map;
		int j;

		/* Not all lines support IRQs */
		for (j = 0; j < ARRAY_SIZE(ixp4xx_gpiomap); j++) {
			map = &ixp4xx_gpiomap[j];
			if (map->gpio_offset == hwirq)
				break;
		}
		if (j == ARRAY_SIZE(ixp4xx_gpiomap)) {
			dev_err(g->dev, "can't look up hwirq %lu\n", hwirq);
			return -EINVAL;
		}
		dev_dbg(g->dev, "found parent hwirq %u\n", map->parent_hwirq);

		/*
		 * We set handle_bad_irq because the .set_type() should
		 * always be invoked and set the right type of handler.
		 */
		irq_domain_set_info(d,
				    irq + i,
				    hwirq + i,
				    &ixp4xx_gpio_irqchip,
				    g,
				    handle_bad_irq,
				    NULL, NULL);
		irq_set_probe(irq + i);

		/*
		 * Create a IRQ fwspec to send up to the parent irqdomain:
		 * specify the hwirq we address on the parent and tie it
		 * all together up the chain.
		 */
		parent_fwspec.fwnode = d->parent->fwnode;
		parent_fwspec.param_count = 2;
		parent_fwspec.param[0] = map->parent_hwirq;
		/* This parent only handles asserted level IRQs */
		parent_fwspec.param[1] = IRQ_TYPE_LEVEL_HIGH;
		dev_dbg(g->dev, "alloc_irqs_parent for %d parent hwirq %d\n",
			irq + i, map->parent_hwirq);
		ret = irq_domain_alloc_irqs_parent(d, irq + i, 1,
						   &parent_fwspec);
		if (ret)
			dev_err(g->dev,
				"failed to allocate parent hwirq %d for hwirq %lu\n",
				map->parent_hwirq, hwirq);
	}

	return 0;
}

static const struct irq_domain_ops ixp4xx_gpio_irqdomain_ops = {
	.translate = ixp4xx_gpio_irq_domain_translate,
	.alloc = ixp4xx_gpio_irq_domain_alloc,
	.free = irq_domain_free_irqs_common,
};

static int ixp4xx_gpio_probe(struct platform_device *pdev)
{
	unsigned long flags;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct irq_domain *parent;
	struct resource *res;
	struct ixp4xx_gpio *g;
	int ret;
	int i;

	g = devm_kzalloc(dev, sizeof(*g), GFP_KERNEL);
	if (!g)
		return -ENOMEM;
	g->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	g->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(g->base)) {
		dev_err(dev, "ioremap error\n");
		return PTR_ERR(g->base);
	}

	/*
	 * Make sure GPIO 14 and 15 are NOT used as clocks but GPIO on
	 * specific machines.
	 */
	if (machine_is_dsmg600() || machine_is_nas100d())
		__raw_writel(0x0, g->base + IXP4XX_REG_GPCLK);

	/*
	 * This is a very special big-endian ARM issue: when the IXP4xx is
	 * run in big endian mode, all registers in the machine are switched
	 * around to the CPU-native endianness. As you see mostly in the
	 * driver we use __raw_readl()/__raw_writel() to access the registers
	 * in the appropriate order. With the GPIO library we need to specify
	 * byte order explicitly, so this flag needs to be set when compiling
	 * for big endian.
	 */
#if defined(CONFIG_CPU_BIG_ENDIAN)
	flags = BGPIOF_BIG_ENDIAN_BYTE_ORDER;
#else
	flags = 0;
#endif

	/* Populate and register gpio chip */
	ret = bgpio_init(&g->gc, dev, 4,
			 g->base + IXP4XX_REG_GPIN,
			 g->base + IXP4XX_REG_GPOUT,
			 NULL,
			 NULL,
			 g->base + IXP4XX_REG_GPOE,
			 flags);
	if (ret) {
		dev_err(dev, "unable to init generic GPIO\n");
		return ret;
	}
	g->gc.to_irq = ixp4xx_gpio_to_irq;
	g->gc.ngpio = 16;
	g->gc.label = "IXP4XX_GPIO_CHIP";
	/*
	 * TODO: when we have migrated to device tree and all GPIOs
	 * are fetched using phandles, set this to -1 to get rid of
	 * the fixed gpiochip base.
	 */
	g->gc.base = 0;
	g->gc.parent = &pdev->dev;
	g->gc.owner = THIS_MODULE;

	ret = devm_gpiochip_add_data(dev, &g->gc, g);
	if (ret) {
		dev_err(dev, "failed to add SoC gpiochip\n");
		return ret;
	}

	/*
	 * When we convert to device tree we will simply look up the
	 * parent irqdomain using irq_find_host(parent) as parent comes
	 * from IRQCHIP_DECLARE(), then use of_node_to_fwnode() to get
	 * the fwnode. For now we need this boardfile style code.
	 */
	if (np) {
		struct device_node *irq_parent;

		irq_parent = of_irq_find_parent(np);
		if (!irq_parent) {
			dev_err(dev, "no IRQ parent node\n");
			return -ENODEV;
		}
		parent = irq_find_host(irq_parent);
		if (!parent) {
			dev_err(dev, "no IRQ parent domain\n");
			return -ENODEV;
		}
		g->fwnode = of_node_to_fwnode(np);
	} else {
		parent = ixp4xx_get_irq_domain();
		g->fwnode = irq_domain_alloc_fwnode(g->base);
		if (!g->fwnode) {
			dev_err(dev, "no domain base\n");
			return -ENODEV;
		}
	}
	g->domain = irq_domain_create_hierarchy(parent,
						IRQ_DOMAIN_FLAG_HIERARCHY,
						ARRAY_SIZE(ixp4xx_gpiomap),
						g->fwnode,
						&ixp4xx_gpio_irqdomain_ops,
						g);
	if (!g->domain) {
		irq_domain_free_fwnode(g->fwnode);
		dev_err(dev, "no hierarchical irq domain\n");
		return ret;
	}

	/*
	 * After adding OF support, this is no longer needed: irqs
	 * will be allocated for the respective fwnodes.
	 */
	if (!np) {
		for (i = 0; i < ARRAY_SIZE(ixp4xx_gpiomap); i++) {
			const struct ixp4xx_gpio_map *map = &ixp4xx_gpiomap[i];
			struct irq_fwspec fwspec;

			fwspec.fwnode = g->fwnode;
			/* This is the hwirq for the GPIO line side of things */
			fwspec.param[0] = map->gpio_offset;
			fwspec.param[1] = IRQ_TYPE_EDGE_RISING;
			fwspec.param_count = 2;
			ret = __irq_domain_alloc_irqs(g->domain,
						      -1, /* just pick something */
						      1,
						      NUMA_NO_NODE,
						      &fwspec,
						      false,
						      NULL);
			if (ret < 0) {
				irq_domain_free_fwnode(g->fwnode);
				dev_err(dev,
					"can not allocate irq for GPIO line %d parent hwirq %d in hierarchy domain: %d\n",
					map->gpio_offset, map->parent_hwirq,
					ret);
				return ret;
			}
		}
	}

	platform_set_drvdata(pdev, g);
	dev_info(dev, "IXP4 GPIO @%p registered\n", g->base);

	return 0;
}

static const struct of_device_id ixp4xx_gpio_of_match[] = {
	{
		.compatible = "intel,ixp4xx-gpio",
	},
	{},
};


static struct platform_driver ixp4xx_gpio_driver = {
	.driver = {
		.name		= "ixp4xx-gpio",
		.of_match_table = of_match_ptr(ixp4xx_gpio_of_match),
	},
	.probe = ixp4xx_gpio_probe,
};
builtin_platform_driver(ixp4xx_gpio_driver);
