/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Copyright (C) 2009-2011 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2013 John Crispin <blogic@openwrt.org>
 */

#include <linux/io.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/spinlock.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#define MTK_MAX_BANK		3
#define MTK_BANK_WIDTH		32

enum mediatek_gpio_reg {
	GPIO_REG_CTRL = 0,
	GPIO_REG_POL,
	GPIO_REG_DATA,
	GPIO_REG_DSET,
	GPIO_REG_DCLR,
	GPIO_REG_REDGE,
	GPIO_REG_FEDGE,
	GPIO_REG_HLVL,
	GPIO_REG_LLVL,
	GPIO_REG_STAT,
	GPIO_REG_EDGE,
};

static void __iomem *mediatek_gpio_membase;
static int mediatek_gpio_irq;
static struct irq_domain *mediatek_gpio_irq_domain;

static struct mtk_gc {
	struct gpio_chip chip;
	spinlock_t lock;
	int bank;
	u32 rising;
	u32 falling;
} *gc_map[MTK_MAX_BANK];

static inline struct mtk_gc
*to_mediatek_gpio(struct gpio_chip *chip)
{
	struct mtk_gc *mgc;

	mgc = container_of(chip, struct mtk_gc, chip);

	return mgc;
}

static inline void
mtk_gpio_w32(struct mtk_gc *rg, u8 reg, u32 val)
{
	iowrite32(val, mediatek_gpio_membase + (reg * 0x10) + (rg->bank * 0x4));
}

static inline u32
mtk_gpio_r32(struct mtk_gc *rg, u8 reg)
{
	return ioread32(mediatek_gpio_membase + (reg * 0x10) + (rg->bank * 0x4));
}

static void
mediatek_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct mtk_gc *rg = to_mediatek_gpio(chip);

	mtk_gpio_w32(rg, (value) ? GPIO_REG_DSET : GPIO_REG_DCLR, BIT(offset));
}

static int
mediatek_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct mtk_gc *rg = to_mediatek_gpio(chip);

	return !!(mtk_gpio_r32(rg, GPIO_REG_DATA) & BIT(offset));
}

static int
mediatek_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct mtk_gc *rg = to_mediatek_gpio(chip);
	unsigned long flags;
	u32 t;

	spin_lock_irqsave(&rg->lock, flags);
	t = mtk_gpio_r32(rg, GPIO_REG_CTRL);
	t &= ~BIT(offset);
	mtk_gpio_w32(rg, GPIO_REG_CTRL, t);
	spin_unlock_irqrestore(&rg->lock, flags);

	return 0;
}

static int
mediatek_gpio_direction_output(struct gpio_chip *chip,
					unsigned offset, int value)
{
	struct mtk_gc *rg = to_mediatek_gpio(chip);
	unsigned long flags;
	u32 t;

	spin_lock_irqsave(&rg->lock, flags);
	t = mtk_gpio_r32(rg, GPIO_REG_CTRL);
	t |= BIT(offset);
	mtk_gpio_w32(rg, GPIO_REG_CTRL, t);
	mediatek_gpio_set(chip, offset, value);
	spin_unlock_irqrestore(&rg->lock, flags);

	return 0;
}

static int
mediatek_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	struct mtk_gc *rg = to_mediatek_gpio(chip);
	unsigned long flags;
	u32 t;

	spin_lock_irqsave(&rg->lock, flags);
	t = mtk_gpio_r32(rg, GPIO_REG_CTRL);
	spin_unlock_irqrestore(&rg->lock, flags);

	if (t & BIT(offset))
		return 0;

	return 1;
}

static int
mediatek_gpio_to_irq(struct gpio_chip *chip, unsigned pin)
{
	struct mtk_gc *rg = to_mediatek_gpio(chip);

	return irq_create_mapping(mediatek_gpio_irq_domain, pin + (rg->bank * MTK_BANK_WIDTH));
}

static int
mediatek_gpio_bank_probe(struct platform_device *pdev, struct device_node *bank)
{
	const __be32 *id = of_get_property(bank, "reg", NULL);
	struct mtk_gc *rg = devm_kzalloc(&pdev->dev,
				sizeof(struct mtk_gc), GFP_KERNEL);

	if (!rg || !id || be32_to_cpu(*id) > MTK_MAX_BANK)
		return -ENOMEM;

	gc_map[be32_to_cpu(*id)] = rg;

	memset(rg, 0, sizeof(struct mtk_gc));

	spin_lock_init(&rg->lock);

	rg->chip.parent = &pdev->dev;
	rg->chip.label = dev_name(&pdev->dev);
	rg->chip.of_node = bank;
	rg->chip.base = MTK_BANK_WIDTH * be32_to_cpu(*id);
	rg->chip.ngpio = MTK_BANK_WIDTH;
	rg->chip.direction_input = mediatek_gpio_direction_input;
	rg->chip.direction_output = mediatek_gpio_direction_output;
	rg->chip.get_direction = mediatek_gpio_get_direction;
	rg->chip.get = mediatek_gpio_get;
	rg->chip.set = mediatek_gpio_set;
	if (mediatek_gpio_irq_domain)
		rg->chip.to_irq = mediatek_gpio_to_irq;
	rg->bank = be32_to_cpu(*id);

	/* set polarity to low for all gpios */
	mtk_gpio_w32(rg, GPIO_REG_POL, 0);

	dev_info(&pdev->dev, "registering %d gpios\n", rg->chip.ngpio);

	return gpiochip_add(&rg->chip);
}

static void
mediatek_gpio_irq_handler(struct irq_desc *desc)
{
	int i;

	for (i = 0; i < MTK_MAX_BANK; i++) {
		struct mtk_gc *rg = gc_map[i];
		unsigned long pending;
		int bit;

		if (!rg)
			continue;

		pending = mtk_gpio_r32(rg, GPIO_REG_STAT);

		for_each_set_bit(bit, &pending, MTK_BANK_WIDTH) {
			u32 map = irq_find_mapping(mediatek_gpio_irq_domain, (MTK_BANK_WIDTH * i) + bit);

			generic_handle_irq(map);
			mtk_gpio_w32(rg, GPIO_REG_STAT, BIT(bit));
		}
	}
}

static void
mediatek_gpio_irq_unmask(struct irq_data *d)
{
	int pin = d->hwirq;
	int bank = pin / 32;
	struct mtk_gc *rg = gc_map[bank];
	unsigned long flags;
	u32 rise, fall;

	if (!rg)
		return;

	rise = mtk_gpio_r32(rg, GPIO_REG_REDGE);
	fall = mtk_gpio_r32(rg, GPIO_REG_FEDGE);

	spin_lock_irqsave(&rg->lock, flags);
	mtk_gpio_w32(rg, GPIO_REG_REDGE, rise | (BIT(d->hwirq) & rg->rising));
	mtk_gpio_w32(rg, GPIO_REG_FEDGE, fall | (BIT(d->hwirq) & rg->falling));
	spin_unlock_irqrestore(&rg->lock, flags);
}

static void
mediatek_gpio_irq_mask(struct irq_data *d)
{
	int pin = d->hwirq;
	int bank = pin / 32;
	struct mtk_gc *rg = gc_map[bank];
	unsigned long flags;
	u32 rise, fall;

	if (!rg)
		return;

	rise = mtk_gpio_r32(rg, GPIO_REG_REDGE);
	fall = mtk_gpio_r32(rg, GPIO_REG_FEDGE);

	spin_lock_irqsave(&rg->lock, flags);
	mtk_gpio_w32(rg, GPIO_REG_FEDGE, fall & ~BIT(d->hwirq));
	mtk_gpio_w32(rg, GPIO_REG_REDGE, rise & ~BIT(d->hwirq));
	spin_unlock_irqrestore(&rg->lock, flags);
}

static int
mediatek_gpio_irq_type(struct irq_data *d, unsigned int type)
{
	int pin = d->hwirq;
	int bank = pin / 32;
	struct mtk_gc *rg = gc_map[bank];
	u32 mask = BIT(d->hwirq);

	if (!rg)
		return -1;

	if (type == IRQ_TYPE_PROBE) {
		if ((rg->rising | rg->falling) & mask)
			return 0;

		type = IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING;
	}

	if (type & IRQ_TYPE_EDGE_RISING)
		rg->rising |= mask;
	else
		rg->rising &= ~mask;

	if (type & IRQ_TYPE_EDGE_FALLING)
		rg->falling |= mask;
	else
		rg->falling &= ~mask;

	return 0;
}

static struct irq_chip mediatek_gpio_irq_chip = {
	.name		= "GPIO",
	.irq_unmask	= mediatek_gpio_irq_unmask,
	.irq_mask	= mediatek_gpio_irq_mask,
	.irq_mask_ack	= mediatek_gpio_irq_mask,
	.irq_set_type	= mediatek_gpio_irq_type,
};

static int
mediatek_gpio_gpio_map(struct irq_domain *d, unsigned int irq, irq_hw_number_t hw)
{
	irq_set_chip_and_handler(irq, &mediatek_gpio_irq_chip, handle_level_irq);
	irq_set_handler_data(irq, d);

	return 0;
}

static const struct irq_domain_ops irq_domain_ops = {
	.xlate = irq_domain_xlate_onecell,
	.map = mediatek_gpio_gpio_map,
};

static int
mediatek_gpio_probe(struct platform_device *pdev)
{
	struct device_node *bank, *np = pdev->dev.of_node;
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	mediatek_gpio_membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mediatek_gpio_membase))
		return PTR_ERR(mediatek_gpio_membase);

	mediatek_gpio_irq = irq_of_parse_and_map(np, 0);
	if (mediatek_gpio_irq) {
		mediatek_gpio_irq_domain = irq_domain_add_linear(np,
			MTK_MAX_BANK * MTK_BANK_WIDTH,
			&irq_domain_ops, NULL);
		if (!mediatek_gpio_irq_domain)
			dev_err(&pdev->dev, "irq_domain_add_linear failed\n");
	}

	for_each_child_of_node(np, bank)
		if (of_device_is_compatible(bank, "mtk,mt7621-gpio-bank"))
			mediatek_gpio_bank_probe(pdev, bank);

	if (mediatek_gpio_irq_domain)
		irq_set_chained_handler(mediatek_gpio_irq, mediatek_gpio_irq_handler);

	return 0;
}

static const struct of_device_id mediatek_gpio_match[] = {
	{ .compatible = "mtk,mt7621-gpio" },
	{},
};
MODULE_DEVICE_TABLE(of, mediatek_gpio_match);

static struct platform_driver mediatek_gpio_driver = {
	.probe = mediatek_gpio_probe,
	.driver = {
		.name = "mt7621_gpio",
		.of_match_table = mediatek_gpio_match,
	},
};

static int __init
mediatek_gpio_init(void)
{
	return platform_driver_register(&mediatek_gpio_driver);
}

subsys_initcall(mediatek_gpio_init);
