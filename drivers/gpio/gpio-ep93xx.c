/*
 * Generic EP93xx GPIO handling
 *
 * Copyright (c) 2008 Ryan Mallon
 * Copyright (c) 2011 H Hartley Sweeten <hsweeten@visionengravers.com>
 *
 * Based on code originally from:
 *  linux/arch/arm/mach-ep93xx/core.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/basic_mmio_gpio.h>

#include <mach/hardware.h>
#include <mach/gpio-ep93xx.h>

#define irq_to_gpio(irq)	((irq) - gpio_to_irq(0))

struct ep93xx_gpio {
	void __iomem		*mmio_base;
	struct bgpio_chip	bgc[8];
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

static void ep93xx_gpio_update_int_params(unsigned port)
{
	BUG_ON(port > 2);

	__raw_writeb(0, EP93XX_GPIO_REG(int_en_register_offset[port]));

	__raw_writeb(gpio_int_type2[port],
		EP93XX_GPIO_REG(int_type2_register_offset[port]));

	__raw_writeb(gpio_int_type1[port],
		EP93XX_GPIO_REG(int_type1_register_offset[port]));

	__raw_writeb(gpio_int_unmasked[port] & gpio_int_enabled[port],
		EP93XX_GPIO_REG(int_en_register_offset[port]));
}

static inline void ep93xx_gpio_int_mask(unsigned line)
{
	gpio_int_unmasked[line >> 3] &= ~(1 << (line & 7));
}

static void ep93xx_gpio_int_debounce(unsigned int irq, bool enable)
{
	int line = irq_to_gpio(irq);
	int port = line >> 3;
	int port_mask = 1 << (line & 7);

	if (enable)
		gpio_int_debounce[port] |= port_mask;
	else
		gpio_int_debounce[port] &= ~port_mask;

	__raw_writeb(gpio_int_debounce[port],
		EP93XX_GPIO_REG(int_debounce_register_offset[port]));
}

static void ep93xx_gpio_ab_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	unsigned char status;
	int i;

	status = __raw_readb(EP93XX_GPIO_A_INT_STATUS);
	for (i = 0; i < 8; i++) {
		if (status & (1 << i)) {
			int gpio_irq = gpio_to_irq(EP93XX_GPIO_LINE_A(0)) + i;
			generic_handle_irq(gpio_irq);
		}
	}

	status = __raw_readb(EP93XX_GPIO_B_INT_STATUS);
	for (i = 0; i < 8; i++) {
		if (status & (1 << i)) {
			int gpio_irq = gpio_to_irq(EP93XX_GPIO_LINE_B(0)) + i;
			generic_handle_irq(gpio_irq);
		}
	}
}

static void ep93xx_gpio_f_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	/*
	 * map discontiguous hw irq range to continuous sw irq range:
	 *
	 *  IRQ_EP93XX_GPIO{0..7}MUX -> gpio_to_irq(EP93XX_GPIO_LINE_F({0..7})
	 */
	int port_f_idx = ((irq + 1) & 7) ^ 4; /* {19..22,47..50} -> {0..7} */
	int gpio_irq = gpio_to_irq(EP93XX_GPIO_LINE_F(0)) + port_f_idx;

	generic_handle_irq(gpio_irq);
}

static void ep93xx_gpio_irq_ack(struct irq_data *d)
{
	int line = irq_to_gpio(d->irq);
	int port = line >> 3;
	int port_mask = 1 << (line & 7);

	if (irqd_get_trigger_type(d) == IRQ_TYPE_EDGE_BOTH) {
		gpio_int_type2[port] ^= port_mask; /* switch edge direction */
		ep93xx_gpio_update_int_params(port);
	}

	__raw_writeb(port_mask, EP93XX_GPIO_REG(eoi_register_offset[port]));
}

static void ep93xx_gpio_irq_mask_ack(struct irq_data *d)
{
	int line = irq_to_gpio(d->irq);
	int port = line >> 3;
	int port_mask = 1 << (line & 7);

	if (irqd_get_trigger_type(d) == IRQ_TYPE_EDGE_BOTH)
		gpio_int_type2[port] ^= port_mask; /* switch edge direction */

	gpio_int_unmasked[port] &= ~port_mask;
	ep93xx_gpio_update_int_params(port);

	__raw_writeb(port_mask, EP93XX_GPIO_REG(eoi_register_offset[port]));
}

static void ep93xx_gpio_irq_mask(struct irq_data *d)
{
	int line = irq_to_gpio(d->irq);
	int port = line >> 3;

	gpio_int_unmasked[port] &= ~(1 << (line & 7));
	ep93xx_gpio_update_int_params(port);
}

static void ep93xx_gpio_irq_unmask(struct irq_data *d)
{
	int line = irq_to_gpio(d->irq);
	int port = line >> 3;

	gpio_int_unmasked[port] |= 1 << (line & 7);
	ep93xx_gpio_update_int_params(port);
}

/*
 * gpio_int_type1 controls whether the interrupt is level (0) or
 * edge (1) triggered, while gpio_int_type2 controls whether it
 * triggers on low/falling (0) or high/rising (1).
 */
static int ep93xx_gpio_irq_type(struct irq_data *d, unsigned int type)
{
	const int gpio = irq_to_gpio(d->irq);
	const int port = gpio >> 3;
	const int port_mask = 1 << (gpio & 7);
	irq_flow_handler_t handler;

	gpio_direction_input(gpio);

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
		if (gpio_get_value(gpio))
			gpio_int_type2[port] &= ~port_mask; /* falling */
		else
			gpio_int_type2[port] |= port_mask; /* rising */
		handler = handle_edge_irq;
		break;
	default:
		pr_err("failed to set irq type %d for gpio %d\n", type, gpio);
		return -EINVAL;
	}

	__irq_set_handler_locked(d->irq, handler);

	gpio_int_enabled[port] |= port_mask;

	ep93xx_gpio_update_int_params(port);

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

static void ep93xx_gpio_init_irq(void)
{
	int gpio_irq;

	for (gpio_irq = gpio_to_irq(0);
	     gpio_irq <= gpio_to_irq(EP93XX_GPIO_LINE_MAX_IRQ); ++gpio_irq) {
		irq_set_chip_and_handler(gpio_irq, &ep93xx_gpio_irq_chip,
					 handle_level_irq);
		set_irq_flags(gpio_irq, IRQF_VALID);
	}

	irq_set_chained_handler(IRQ_EP93XX_GPIO_AB,
				ep93xx_gpio_ab_irq_handler);
	irq_set_chained_handler(IRQ_EP93XX_GPIO0MUX,
				ep93xx_gpio_f_irq_handler);
	irq_set_chained_handler(IRQ_EP93XX_GPIO1MUX,
				ep93xx_gpio_f_irq_handler);
	irq_set_chained_handler(IRQ_EP93XX_GPIO2MUX,
				ep93xx_gpio_f_irq_handler);
	irq_set_chained_handler(IRQ_EP93XX_GPIO3MUX,
				ep93xx_gpio_f_irq_handler);
	irq_set_chained_handler(IRQ_EP93XX_GPIO4MUX,
				ep93xx_gpio_f_irq_handler);
	irq_set_chained_handler(IRQ_EP93XX_GPIO5MUX,
				ep93xx_gpio_f_irq_handler);
	irq_set_chained_handler(IRQ_EP93XX_GPIO6MUX,
				ep93xx_gpio_f_irq_handler);
	irq_set_chained_handler(IRQ_EP93XX_GPIO7MUX,
				ep93xx_gpio_f_irq_handler);
}


/*************************************************************************
 * gpiolib interface for EP93xx on-chip GPIOs
 *************************************************************************/
struct ep93xx_gpio_bank {
	const char	*label;
	int		data;
	int		dir;
	int		base;
	bool		has_debounce;
};

#define EP93XX_GPIO_BANK(_label, _data, _dir, _base, _debounce)	\
	{							\
		.label		= _label,			\
		.data		= _data,			\
		.dir		= _dir,				\
		.base		= _base,			\
		.has_debounce	= _debounce,			\
	}

static struct ep93xx_gpio_bank ep93xx_gpio_banks[] = {
	EP93XX_GPIO_BANK("A", 0x00, 0x10, 0, true),
	EP93XX_GPIO_BANK("B", 0x04, 0x14, 8, true),
	EP93XX_GPIO_BANK("C", 0x08, 0x18, 40, false),
	EP93XX_GPIO_BANK("D", 0x0c, 0x1c, 24, false),
	EP93XX_GPIO_BANK("E", 0x20, 0x24, 32, false),
	EP93XX_GPIO_BANK("F", 0x30, 0x34, 16, true),
	EP93XX_GPIO_BANK("G", 0x38, 0x3c, 48, false),
	EP93XX_GPIO_BANK("H", 0x40, 0x44, 56, false),
};

static int ep93xx_gpio_set_debounce(struct gpio_chip *chip,
				    unsigned offset, unsigned debounce)
{
	int gpio = chip->base + offset;
	int irq = gpio_to_irq(gpio);

	if (irq < 0)
		return -EINVAL;

	ep93xx_gpio_int_debounce(irq, debounce ? true : false);

	return 0;
}

/*
 * Map GPIO A0..A7  (0..7)  to irq 64..71,
 *          B0..B7  (7..15) to irq 72..79, and
 *          F0..F7 (16..24) to irq 80..87.
 */
static int ep93xx_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	int gpio = chip->base + offset;

	if (gpio > EP93XX_GPIO_LINE_MAX_IRQ)
		return -EINVAL;

	return 64 + gpio;
}

static int ep93xx_gpio_add_bank(struct bgpio_chip *bgc, struct device *dev,
	void __iomem *mmio_base, struct ep93xx_gpio_bank *bank)
{
	void __iomem *data = mmio_base + bank->data;
	void __iomem *dir =  mmio_base + bank->dir;
	int err;

	err = bgpio_init(bgc, dev, 1, data, NULL, NULL, dir, NULL, false);
	if (err)
		return err;

	bgc->gc.label = bank->label;
	bgc->gc.base = bank->base;

	if (bank->has_debounce) {
		bgc->gc.set_debounce = ep93xx_gpio_set_debounce;
		bgc->gc.to_irq = ep93xx_gpio_to_irq;
	}

	return gpiochip_add(&bgc->gc);
}

static int __devinit ep93xx_gpio_probe(struct platform_device *pdev)
{
	struct ep93xx_gpio *ep93xx_gpio;
	struct resource *res;
	void __iomem *mmio;
	int i;
	int ret;

	ep93xx_gpio = kzalloc(sizeof(*ep93xx_gpio), GFP_KERNEL);
	if (!ep93xx_gpio)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENXIO;
		goto exit_free;
	}

	if (!request_mem_region(res->start, resource_size(res), pdev->name)) {
		ret = -EBUSY;
		goto exit_free;
	}

	mmio = ioremap(res->start, resource_size(res));
	if (!mmio) {
		ret = -ENXIO;
		goto exit_release;
	}
	ep93xx_gpio->mmio_base = mmio;

	for (i = 0; i < ARRAY_SIZE(ep93xx_gpio_banks); i++) {
		struct bgpio_chip *bgc = &ep93xx_gpio->bgc[i];
		struct ep93xx_gpio_bank *bank = &ep93xx_gpio_banks[i];

		if (ep93xx_gpio_add_bank(bgc, &pdev->dev, mmio, bank))
			dev_warn(&pdev->dev, "Unable to add gpio bank %s\n",
				bank->label);
	}

	ep93xx_gpio_init_irq();

	return 0;

exit_release:
	release_mem_region(res->start, resource_size(res));
exit_free:
	kfree(ep93xx_gpio);
	dev_info(&pdev->dev, "%s failed with errno %d\n", __func__, ret);
	return ret;
}

static struct platform_driver ep93xx_gpio_driver = {
	.driver		= {
		.name	= "gpio-ep93xx",
		.owner	= THIS_MODULE,
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
