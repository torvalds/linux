// SPDX-License-Identifier: GPL-2.0
/*
 * Generic EP93xx GPIO handling
 *
 * Copyright (c) 2008 Ryan Mallon
 * Copyright (c) 2011 H Hartley Sweeten <hsweeten@visionengravers.com>
 *
 * Based on code originally from:
 *  linux/arch/arm/mach-ep93xx/core.c
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/gpio/driver.h>
#include <linux/bitops.h>
#include <linux/seq_file.h>

#define EP93XX_GPIO_F_INT_STATUS 0x5c
#define EP93XX_GPIO_A_INT_STATUS 0xa0
#define EP93XX_GPIO_B_INT_STATUS 0xbc

/* Maximum value for gpio line identifiers */
#define EP93XX_GPIO_LINE_MAX 63

/* Number of GPIO chips in EP93XX */
#define EP93XX_GPIO_CHIP_NUM 8

/* Maximum value for irq capable line identifiers */
#define EP93XX_GPIO_LINE_MAX_IRQ 23

#define EP93XX_GPIO_A_IRQ_BASE 64
#define EP93XX_GPIO_B_IRQ_BASE 72
/*
 * Static mapping of GPIO bank F IRQS:
 * F0..F7 (16..24) to irq 80..87.
 */
#define EP93XX_GPIO_F_IRQ_BASE 80

struct ep93xx_gpio_irq_chip {
	u8 irq_offset;
	u8 int_unmasked;
	u8 int_enabled;
	u8 int_type1;
	u8 int_type2;
	u8 int_debounce;
};

struct ep93xx_gpio_chip {
	struct gpio_chip		gc;
	struct ep93xx_gpio_irq_chip	*eic;
};

struct ep93xx_gpio {
	void __iomem		*base;
	struct ep93xx_gpio_chip	gc[EP93XX_GPIO_CHIP_NUM];
};

#define to_ep93xx_gpio_chip(x) container_of(x, struct ep93xx_gpio_chip, gc)

static struct ep93xx_gpio_irq_chip *to_ep93xx_gpio_irq_chip(struct gpio_chip *gc)
{
	struct ep93xx_gpio_chip *egc = to_ep93xx_gpio_chip(gc);

	return egc->eic;
}

/*************************************************************************
 * Interrupt handling for EP93xx on-chip GPIOs
 *************************************************************************/
#define EP93XX_INT_TYPE1_OFFSET		0x00
#define EP93XX_INT_TYPE2_OFFSET		0x04
#define EP93XX_INT_EOI_OFFSET		0x08
#define EP93XX_INT_EN_OFFSET		0x0c
#define EP93XX_INT_STATUS_OFFSET	0x10
#define EP93XX_INT_RAW_STATUS_OFFSET	0x14
#define EP93XX_INT_DEBOUNCE_OFFSET	0x18

static void ep93xx_gpio_update_int_params(struct ep93xx_gpio *epg,
					  struct ep93xx_gpio_irq_chip *eic)
{
	writeb_relaxed(0, epg->base + eic->irq_offset + EP93XX_INT_EN_OFFSET);

	writeb_relaxed(eic->int_type2,
		       epg->base + eic->irq_offset + EP93XX_INT_TYPE2_OFFSET);

	writeb_relaxed(eic->int_type1,
		       epg->base + eic->irq_offset + EP93XX_INT_TYPE1_OFFSET);

	writeb_relaxed(eic->int_unmasked & eic->int_enabled,
		       epg->base + eic->irq_offset + EP93XX_INT_EN_OFFSET);
}

static void ep93xx_gpio_int_debounce(struct gpio_chip *gc,
				     unsigned int offset, bool enable)
{
	struct ep93xx_gpio *epg = gpiochip_get_data(gc);
	struct ep93xx_gpio_irq_chip *eic = to_ep93xx_gpio_irq_chip(gc);
	int port_mask = BIT(offset);

	if (enable)
		eic->int_debounce |= port_mask;
	else
		eic->int_debounce &= ~port_mask;

	writeb(eic->int_debounce,
	       epg->base + eic->irq_offset + EP93XX_INT_DEBOUNCE_OFFSET);
}

static void ep93xx_gpio_ab_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct ep93xx_gpio *epg = gpiochip_get_data(gc);
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	unsigned long stat;
	int offset;

	chained_irq_enter(irqchip, desc);

	/*
	 * Dispatch the IRQs to the irqdomain of each A and B
	 * gpiochip irqdomains depending on what has fired.
	 * The tricky part is that the IRQ line is shared
	 * between bank A and B and each has their own gpiochip.
	 */
	stat = readb(epg->base + EP93XX_GPIO_A_INT_STATUS);
	for_each_set_bit(offset, &stat, 8)
		generic_handle_domain_irq(epg->gc[0].gc.irq.domain,
					  offset);

	stat = readb(epg->base + EP93XX_GPIO_B_INT_STATUS);
	for_each_set_bit(offset, &stat, 8)
		generic_handle_domain_irq(epg->gc[1].gc.irq.domain,
					  offset);

	chained_irq_exit(irqchip, desc);
}

static void ep93xx_gpio_f_irq_handler(struct irq_desc *desc)
{
	/*
	 * map discontiguous hw irq range to continuous sw irq range:
	 *
	 *  IRQ_EP93XX_GPIO{0..7}MUX -> EP93XX_GPIO_LINE_F{0..7}
	 */
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	unsigned int irq = irq_desc_get_irq(desc);
	int port_f_idx = (irq & 7) ^ 4; /* {20..23,48..51} -> {0..7} */
	int gpio_irq = EP93XX_GPIO_F_IRQ_BASE + port_f_idx;

	chained_irq_enter(irqchip, desc);
	generic_handle_irq(gpio_irq);
	chained_irq_exit(irqchip, desc);
}

static void ep93xx_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct ep93xx_gpio_irq_chip *eic = to_ep93xx_gpio_irq_chip(gc);
	struct ep93xx_gpio *epg = gpiochip_get_data(gc);
	int port_mask = BIT(d->irq & 7);

	if (irqd_get_trigger_type(d) == IRQ_TYPE_EDGE_BOTH) {
		eic->int_type2 ^= port_mask; /* switch edge direction */
		ep93xx_gpio_update_int_params(epg, eic);
	}

	writeb(port_mask, epg->base + eic->irq_offset + EP93XX_INT_EOI_OFFSET);
}

static void ep93xx_gpio_irq_mask_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct ep93xx_gpio_irq_chip *eic = to_ep93xx_gpio_irq_chip(gc);
	struct ep93xx_gpio *epg = gpiochip_get_data(gc);
	int port_mask = BIT(d->irq & 7);

	if (irqd_get_trigger_type(d) == IRQ_TYPE_EDGE_BOTH)
		eic->int_type2 ^= port_mask; /* switch edge direction */

	eic->int_unmasked &= ~port_mask;
	ep93xx_gpio_update_int_params(epg, eic);

	writeb(port_mask, epg->base + eic->irq_offset + EP93XX_INT_EOI_OFFSET);
	gpiochip_disable_irq(gc, irqd_to_hwirq(d));
}

static void ep93xx_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct ep93xx_gpio_irq_chip *eic = to_ep93xx_gpio_irq_chip(gc);
	struct ep93xx_gpio *epg = gpiochip_get_data(gc);

	eic->int_unmasked &= ~BIT(d->irq & 7);
	ep93xx_gpio_update_int_params(epg, eic);
	gpiochip_disable_irq(gc, irqd_to_hwirq(d));
}

static void ep93xx_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct ep93xx_gpio_irq_chip *eic = to_ep93xx_gpio_irq_chip(gc);
	struct ep93xx_gpio *epg = gpiochip_get_data(gc);

	gpiochip_enable_irq(gc, irqd_to_hwirq(d));
	eic->int_unmasked |= BIT(d->irq & 7);
	ep93xx_gpio_update_int_params(epg, eic);
}

/*
 * gpio_int_type1 controls whether the interrupt is level (0) or
 * edge (1) triggered, while gpio_int_type2 controls whether it
 * triggers on low/falling (0) or high/rising (1).
 */
static int ep93xx_gpio_irq_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct ep93xx_gpio_irq_chip *eic = to_ep93xx_gpio_irq_chip(gc);
	struct ep93xx_gpio *epg = gpiochip_get_data(gc);
	int offset = d->irq & 7;
	int port_mask = BIT(offset);
	irq_flow_handler_t handler;

	gc->direction_input(gc, offset);

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		eic->int_type1 |= port_mask;
		eic->int_type2 |= port_mask;
		handler = handle_edge_irq;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		eic->int_type1 |= port_mask;
		eic->int_type2 &= ~port_mask;
		handler = handle_edge_irq;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		eic->int_type1 &= ~port_mask;
		eic->int_type2 |= port_mask;
		handler = handle_level_irq;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		eic->int_type1 &= ~port_mask;
		eic->int_type2 &= ~port_mask;
		handler = handle_level_irq;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		eic->int_type1 |= port_mask;
		/* set initial polarity based on current input level */
		if (gc->get(gc, offset))
			eic->int_type2 &= ~port_mask; /* falling */
		else
			eic->int_type2 |= port_mask; /* rising */
		handler = handle_edge_irq;
		break;
	default:
		return -EINVAL;
	}

	irq_set_handler_locked(d, handler);

	eic->int_enabled |= port_mask;

	ep93xx_gpio_update_int_params(epg, eic);

	return 0;
}

/*************************************************************************
 * gpiolib interface for EP93xx on-chip GPIOs
 *************************************************************************/
struct ep93xx_gpio_bank {
	const char	*label;
	int		data;
	int		dir;
	int		irq;
	int		base;
	bool		has_irq;
	bool		has_hierarchical_irq;
	unsigned int	irq_base;
};

#define EP93XX_GPIO_BANK(_label, _data, _dir, _irq, _base, _has_irq, _has_hier, _irq_base) \
	{							\
		.label		= _label,			\
		.data		= _data,			\
		.dir		= _dir,				\
		.irq		= _irq,				\
		.base		= _base,			\
		.has_irq	= _has_irq,			\
		.has_hierarchical_irq = _has_hier,		\
		.irq_base	= _irq_base,			\
	}

static struct ep93xx_gpio_bank ep93xx_gpio_banks[] = {
	/* Bank A has 8 IRQs */
	EP93XX_GPIO_BANK("A", 0x00, 0x10, 0x90, 0, true, false, EP93XX_GPIO_A_IRQ_BASE),
	/* Bank B has 8 IRQs */
	EP93XX_GPIO_BANK("B", 0x04, 0x14, 0xac, 8, true, false, EP93XX_GPIO_B_IRQ_BASE),
	EP93XX_GPIO_BANK("C", 0x08, 0x18, 0x00, 40, false, false, 0),
	EP93XX_GPIO_BANK("D", 0x0c, 0x1c, 0x00, 24, false, false, 0),
	EP93XX_GPIO_BANK("E", 0x20, 0x24, 0x00, 32, false, false, 0),
	/* Bank F has 8 IRQs */
	EP93XX_GPIO_BANK("F", 0x30, 0x34, 0x4c, 16, false, true, EP93XX_GPIO_F_IRQ_BASE),
	EP93XX_GPIO_BANK("G", 0x38, 0x3c, 0x00, 48, false, false, 0),
	EP93XX_GPIO_BANK("H", 0x40, 0x44, 0x00, 56, false, false, 0),
};

static int ep93xx_gpio_set_config(struct gpio_chip *gc, unsigned offset,
				  unsigned long config)
{
	u32 debounce;

	if (pinconf_to_config_param(config) != PIN_CONFIG_INPUT_DEBOUNCE)
		return -ENOTSUPP;

	debounce = pinconf_to_config_argument(config);
	ep93xx_gpio_int_debounce(gc, offset, debounce ? true : false);

	return 0;
}

static void ep93xx_irq_print_chip(struct irq_data *data, struct seq_file *p)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);

	seq_printf(p, dev_name(gc->parent));
}

static const struct irq_chip gpio_eic_irq_chip = {
	.name			= "ep93xx-gpio-eic",
	.irq_ack		= ep93xx_gpio_irq_ack,
	.irq_mask		= ep93xx_gpio_irq_mask,
	.irq_unmask		= ep93xx_gpio_irq_unmask,
	.irq_mask_ack	= ep93xx_gpio_irq_mask_ack,
	.irq_set_type	= ep93xx_gpio_irq_type,
	.irq_print_chip	= ep93xx_irq_print_chip,
	.flags			= IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static int ep93xx_gpio_add_bank(struct ep93xx_gpio_chip *egc,
				struct platform_device *pdev,
				struct ep93xx_gpio *epg,
				struct ep93xx_gpio_bank *bank)
{
	void __iomem *data = epg->base + bank->data;
	void __iomem *dir = epg->base + bank->dir;
	struct gpio_chip *gc = &egc->gc;
	struct device *dev = &pdev->dev;
	struct gpio_irq_chip *girq;
	int err;

	err = bgpio_init(gc, dev, 1, data, NULL, NULL, dir, NULL, 0);
	if (err)
		return err;

	gc->label = bank->label;
	gc->base = bank->base;

	girq = &gc->irq;
	if (bank->has_irq || bank->has_hierarchical_irq) {
		gc->set_config = ep93xx_gpio_set_config;
		egc->eic = devm_kcalloc(dev, 1,
					sizeof(*egc->eic),
					GFP_KERNEL);
		if (!egc->eic)
			return -ENOMEM;
		egc->eic->irq_offset = bank->irq;
		gpio_irq_chip_set_chip(girq, &gpio_eic_irq_chip);
	}

	if (bank->has_irq) {
		int ab_parent_irq = platform_get_irq(pdev, 0);

		girq->parent_handler = ep93xx_gpio_ab_irq_handler;
		girq->num_parents = 1;
		girq->parents = devm_kcalloc(dev, girq->num_parents,
					     sizeof(*girq->parents),
					     GFP_KERNEL);
		if (!girq->parents)
			return -ENOMEM;
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_level_irq;
		girq->parents[0] = ab_parent_irq;
		girq->first = bank->irq_base;
	}

	/* Only bank F has especially funky IRQ handling */
	if (bank->has_hierarchical_irq) {
		int gpio_irq;
		int i;

		/*
		 * FIXME: convert this to use hierarchical IRQ support!
		 * this requires fixing the root irqchip to be hierarchical.
		 */
		girq->parent_handler = ep93xx_gpio_f_irq_handler;
		girq->num_parents = 8;
		girq->parents = devm_kcalloc(dev, girq->num_parents,
					     sizeof(*girq->parents),
					     GFP_KERNEL);
		if (!girq->parents)
			return -ENOMEM;
		/* Pick resources 1..8 for these IRQs */
		for (i = 0; i < girq->num_parents; i++) {
			girq->parents[i] = platform_get_irq(pdev, i + 1);
			gpio_irq = bank->irq_base + i;
			irq_set_chip_data(gpio_irq, &epg->gc[5]);
			irq_set_chip_and_handler(gpio_irq,
						 girq->chip,
						 handle_level_irq);
			irq_clear_status_flags(gpio_irq, IRQ_NOREQUEST);
		}
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_level_irq;
		girq->first = bank->irq_base;
	}

	return devm_gpiochip_add_data(dev, gc, epg);
}

static int ep93xx_gpio_probe(struct platform_device *pdev)
{
	struct ep93xx_gpio *epg;
	int i;

	epg = devm_kzalloc(&pdev->dev, sizeof(*epg), GFP_KERNEL);
	if (!epg)
		return -ENOMEM;

	epg->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(epg->base))
		return PTR_ERR(epg->base);

	for (i = 0; i < ARRAY_SIZE(ep93xx_gpio_banks); i++) {
		struct ep93xx_gpio_chip *gc = &epg->gc[i];
		struct ep93xx_gpio_bank *bank = &ep93xx_gpio_banks[i];

		if (ep93xx_gpio_add_bank(gc, pdev, epg, bank))
			dev_warn(&pdev->dev, "Unable to add gpio bank %s\n",
				 bank->label);
	}

	return 0;
}

static struct platform_driver ep93xx_gpio_driver = {
	.driver		= {
		.name	= "gpio-ep93xx",
	},
	.probe		= ep93xx_gpio_probe,
};

static int __init ep93xx_gpio_init(void)
{
	return platform_driver_register(&ep93xx_gpio_driver);
}
postcore_initcall(ep93xx_gpio_init);

MODULE_AUTHOR("Ryan Mallon <ryan@bluewatersys.com> "
		"H Hartley Sweeten <hsweeten@visionengravers.com>");
MODULE_DESCRIPTION("EP93XX GPIO driver");
MODULE_LICENSE("GPL");
