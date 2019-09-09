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

#define EP93XX_GPIO_F_INT_STATUS 0x5c
#define EP93XX_GPIO_A_INT_STATUS 0xa0
#define EP93XX_GPIO_B_INT_STATUS 0xbc

/* Maximum value for gpio line identifiers */
#define EP93XX_GPIO_LINE_MAX 63

/* Maximum value for irq capable line identifiers */
#define EP93XX_GPIO_LINE_MAX_IRQ 23

/*
 * Static mapping of GPIO bank F IRQS:
 * F0..F7 (16..24) to irq 80..87.
 */
#define EP93XX_GPIO_F_IRQ_BASE 80

struct ep93xx_gpio {
	void __iomem		*base;
	struct gpio_chip	gc[8];
};

/*************************************************************************
 * Interrupt handling for EP93xx on-chip GPIOs
 *************************************************************************/
static unsigned char gpio_int_unmasked[3];
static unsigned char gpio_int_enabled[3];
static unsigned char gpio_int_type1[3];
static unsigned char gpio_int_type2[3];
static unsigned char gpio_int_debounce[3];

/* Port ordering is: A B F */
static const u8 int_type1_register_offset[3]	= { 0x90, 0xac, 0x4c };
static const u8 int_type2_register_offset[3]	= { 0x94, 0xb0, 0x50 };
static const u8 eoi_register_offset[3]		= { 0x98, 0xb4, 0x54 };
static const u8 int_en_register_offset[3]	= { 0x9c, 0xb8, 0x58 };
static const u8 int_debounce_register_offset[3]	= { 0xa8, 0xc4, 0x64 };

static void ep93xx_gpio_update_int_params(struct ep93xx_gpio *epg, unsigned port)
{
	BUG_ON(port > 2);

	writeb_relaxed(0, epg->base + int_en_register_offset[port]);

	writeb_relaxed(gpio_int_type2[port],
		       epg->base + int_type2_register_offset[port]);

	writeb_relaxed(gpio_int_type1[port],
		       epg->base + int_type1_register_offset[port]);

	writeb(gpio_int_unmasked[port] & gpio_int_enabled[port],
	       epg->base + int_en_register_offset[port]);
}

static int ep93xx_gpio_port(struct gpio_chip *gc)
{
	struct ep93xx_gpio *epg = gpiochip_get_data(gc);
	int port = 0;

	while (port < ARRAY_SIZE(epg->gc) && gc != &epg->gc[port])
		port++;

	/* This should not happen but is there as a last safeguard */
	if (port == ARRAY_SIZE(epg->gc)) {
		pr_crit("can't find the GPIO port\n");
		return 0;
	}

	return port;
}

static void ep93xx_gpio_int_debounce(struct gpio_chip *gc,
				     unsigned int offset, bool enable)
{
	struct ep93xx_gpio *epg = gpiochip_get_data(gc);
	int port = ep93xx_gpio_port(gc);
	int port_mask = BIT(offset);

	if (enable)
		gpio_int_debounce[port] |= port_mask;
	else
		gpio_int_debounce[port] &= ~port_mask;

	writeb(gpio_int_debounce[port],
	       epg->base + int_debounce_register_offset[port]);
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
		generic_handle_irq(irq_find_mapping(epg->gc[0].irq.domain,
						    offset));

	stat = readb(epg->base + EP93XX_GPIO_B_INT_STATUS);
	for_each_set_bit(offset, &stat, 8)
		generic_handle_irq(irq_find_mapping(epg->gc[1].irq.domain,
						    offset));

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
	int port_f_idx = ((irq + 1) & 7) ^ 4; /* {19..22,47..50} -> {0..7} */
	int gpio_irq = EP93XX_GPIO_F_IRQ_BASE + port_f_idx;

	chained_irq_enter(irqchip, desc);
	generic_handle_irq(gpio_irq);
	chained_irq_exit(irqchip, desc);
}

static void ep93xx_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct ep93xx_gpio *epg = gpiochip_get_data(gc);
	int port = ep93xx_gpio_port(gc);
	int port_mask = BIT(d->irq & 7);

	if (irqd_get_trigger_type(d) == IRQ_TYPE_EDGE_BOTH) {
		gpio_int_type2[port] ^= port_mask; /* switch edge direction */
		ep93xx_gpio_update_int_params(epg, port);
	}

	writeb(port_mask, epg->base + eoi_register_offset[port]);
}

static void ep93xx_gpio_irq_mask_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct ep93xx_gpio *epg = gpiochip_get_data(gc);
	int port = ep93xx_gpio_port(gc);
	int port_mask = BIT(d->irq & 7);

	if (irqd_get_trigger_type(d) == IRQ_TYPE_EDGE_BOTH)
		gpio_int_type2[port] ^= port_mask; /* switch edge direction */

	gpio_int_unmasked[port] &= ~port_mask;
	ep93xx_gpio_update_int_params(epg, port);

	writeb(port_mask, epg->base + eoi_register_offset[port]);
}

static void ep93xx_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct ep93xx_gpio *epg = gpiochip_get_data(gc);
	int port = ep93xx_gpio_port(gc);

	gpio_int_unmasked[port] &= ~BIT(d->irq & 7);
	ep93xx_gpio_update_int_params(epg, port);
}

static void ep93xx_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct ep93xx_gpio *epg = gpiochip_get_data(gc);
	int port = ep93xx_gpio_port(gc);

	gpio_int_unmasked[port] |= BIT(d->irq & 7);
	ep93xx_gpio_update_int_params(epg, port);
}

/*
 * gpio_int_type1 controls whether the interrupt is level (0) or
 * edge (1) triggered, while gpio_int_type2 controls whether it
 * triggers on low/falling (0) or high/rising (1).
 */
static int ep93xx_gpio_irq_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct ep93xx_gpio *epg = gpiochip_get_data(gc);
	int port = ep93xx_gpio_port(gc);
	int offset = d->irq & 7;
	int port_mask = BIT(offset);
	irq_flow_handler_t handler;

	gc->direction_input(gc, offset);

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		gpio_int_type1[port] |= port_mask;
		gpio_int_type2[port] |= port_mask;
		handler = handle_edge_irq;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		gpio_int_type1[port] |= port_mask;
		gpio_int_type2[port] &= ~port_mask;
		handler = handle_edge_irq;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		gpio_int_type1[port] &= ~port_mask;
		gpio_int_type2[port] |= port_mask;
		handler = handle_level_irq;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		gpio_int_type1[port] &= ~port_mask;
		gpio_int_type2[port] &= ~port_mask;
		handler = handle_level_irq;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		gpio_int_type1[port] |= port_mask;
		/* set initial polarity based on current input level */
		if (gc->get(gc, offset))
			gpio_int_type2[port] &= ~port_mask; /* falling */
		else
			gpio_int_type2[port] |= port_mask; /* rising */
		handler = handle_edge_irq;
		break;
	default:
		return -EINVAL;
	}

	irq_set_handler_locked(d, handler);

	gpio_int_enabled[port] |= port_mask;

	ep93xx_gpio_update_int_params(epg, port);

	return 0;
}

static struct irq_chip ep93xx_gpio_irq_chip = {
	.name		= "GPIO",
	.irq_ack	= ep93xx_gpio_irq_ack,
	.irq_mask_ack	= ep93xx_gpio_irq_mask_ack,
	.irq_mask	= ep93xx_gpio_irq_mask,
	.irq_unmask	= ep93xx_gpio_irq_unmask,
	.irq_set_type	= ep93xx_gpio_irq_type,
};

static int ep93xx_gpio_init_irq(struct platform_device *pdev,
				struct ep93xx_gpio *epg)
{
	int ab_parent_irq = platform_get_irq(pdev, 0);
	struct device *dev = &pdev->dev;
	int gpio_irq;
	int ret;
	int i;

	/* The A bank */
	ret = gpiochip_irqchip_add(&epg->gc[0], &ep93xx_gpio_irq_chip,
                                   64, handle_level_irq,
                                   IRQ_TYPE_NONE);
	if (ret) {
		dev_err(dev, "Could not add irqchip 0\n");
		return ret;
	}
	gpiochip_set_chained_irqchip(&epg->gc[0], &ep93xx_gpio_irq_chip,
				     ab_parent_irq,
				     ep93xx_gpio_ab_irq_handler);

	/* The B bank */
	ret = gpiochip_irqchip_add(&epg->gc[1], &ep93xx_gpio_irq_chip,
                                   72, handle_level_irq,
                                   IRQ_TYPE_NONE);
	if (ret) {
		dev_err(dev, "Could not add irqchip 1\n");
		return ret;
	}
	gpiochip_set_chained_irqchip(&epg->gc[1], &ep93xx_gpio_irq_chip,
				     ab_parent_irq,
				     ep93xx_gpio_ab_irq_handler);

	/* The F bank */
	for (i = 0; i < 8; i++) {
		gpio_irq = EP93XX_GPIO_F_IRQ_BASE + i;
		irq_set_chip_data(gpio_irq, &epg->gc[5]);
		irq_set_chip_and_handler(gpio_irq, &ep93xx_gpio_irq_chip,
					 handle_level_irq);
		irq_clear_status_flags(gpio_irq, IRQ_NOREQUEST);
	}

	for (i = 1; i <= 8; i++)
		irq_set_chained_handler_and_data(platform_get_irq(pdev, i),
						 ep93xx_gpio_f_irq_handler,
						 &epg->gc[i]);
	return 0;
}


/*************************************************************************
 * gpiolib interface for EP93xx on-chip GPIOs
 *************************************************************************/
struct ep93xx_gpio_bank {
	const char	*label;
	int		data;
	int		dir;
	int		base;
	bool		has_irq;
};

#define EP93XX_GPIO_BANK(_label, _data, _dir, _base, _has_irq)	\
	{							\
		.label		= _label,			\
		.data		= _data,			\
		.dir		= _dir,				\
		.base		= _base,			\
		.has_irq	= _has_irq,			\
	}

static struct ep93xx_gpio_bank ep93xx_gpio_banks[] = {
	EP93XX_GPIO_BANK("A", 0x00, 0x10, 0, true), /* Bank A has 8 IRQs */
	EP93XX_GPIO_BANK("B", 0x04, 0x14, 8, true), /* Bank B has 8 IRQs */
	EP93XX_GPIO_BANK("C", 0x08, 0x18, 40, false),
	EP93XX_GPIO_BANK("D", 0x0c, 0x1c, 24, false),
	EP93XX_GPIO_BANK("E", 0x20, 0x24, 32, false),
	EP93XX_GPIO_BANK("F", 0x30, 0x34, 16, true), /* Bank F has 8 IRQs */
	EP93XX_GPIO_BANK("G", 0x38, 0x3c, 48, false),
	EP93XX_GPIO_BANK("H", 0x40, 0x44, 56, false),
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

static int ep93xx_gpio_f_to_irq(struct gpio_chip *gc, unsigned offset)
{
	return EP93XX_GPIO_F_IRQ_BASE + offset;
}

static int ep93xx_gpio_add_bank(struct gpio_chip *gc, struct device *dev,
				struct ep93xx_gpio *epg,
				struct ep93xx_gpio_bank *bank)
{
	void __iomem *data = epg->base + bank->data;
	void __iomem *dir = epg->base + bank->dir;
	int err;

	err = bgpio_init(gc, dev, 1, data, NULL, NULL, dir, NULL, 0);
	if (err)
		return err;

	gc->label = bank->label;
	gc->base = bank->base;

	if (bank->has_irq)
		gc->set_config = ep93xx_gpio_set_config;

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
		struct gpio_chip *gc = &epg->gc[i];
		struct ep93xx_gpio_bank *bank = &ep93xx_gpio_banks[i];

		if (ep93xx_gpio_add_bank(gc, &pdev->dev, epg, bank))
			dev_warn(&pdev->dev, "Unable to add gpio bank %s\n",
				 bank->label);
		/* Only bank F has especially funky IRQ handling */
		if (i == 5)
			gc->to_irq = ep93xx_gpio_f_to_irq;
	}

	ep93xx_gpio_init_irq(pdev, epg);

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
