// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2009-2011 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2013 John Crispin <blogic@openwrt.org>
 */

#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#define MTK_BANK_CNT		3
#define MTK_BANK_WIDTH		32
#define PIN_MASK(nr)		(1UL << ((nr % MTK_BANK_WIDTH)))

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

struct mtk_gc {
	struct gpio_chip chip;
	spinlock_t lock;
	int bank;
	u32 rising;
	u32 falling;
};

struct mtk_data {
	void __iomem *gpio_membase;
	int gpio_irq;
	struct irq_domain *gpio_irq_domain;
	struct mtk_gc gc_map[MTK_BANK_CNT];
};

static inline struct mtk_gc *
to_mediatek_gpio(struct gpio_chip *chip)
{
	return container_of(chip, struct mtk_gc, chip);
}

static inline void
mtk_gpio_w32(struct mtk_gc *rg, u8 reg, u32 val)
{
	struct mtk_data *gpio_data = gpiochip_get_data(&rg->chip);
	u32 offset = (reg * 0x10) + (rg->bank * 0x4);

	iowrite32(val, gpio_data->gpio_membase + offset);
}

static inline u32
mtk_gpio_r32(struct mtk_gc *rg, u8 reg)
{
	struct mtk_data *gpio_data = gpiochip_get_data(&rg->chip);
	u32 offset = (reg * 0x10) + (rg->bank * 0x4);

	return ioread32(gpio_data->gpio_membase + offset);
}

static void
mediatek_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct mtk_gc *rg = to_mediatek_gpio(chip);

	mtk_gpio_w32(rg, (value) ? GPIO_REG_DSET : GPIO_REG_DCLR, BIT(offset));
}

static int
mediatek_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct mtk_gc *rg = to_mediatek_gpio(chip);

	return !!(mtk_gpio_r32(rg, GPIO_REG_DATA) & BIT(offset));
}

static int
mediatek_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
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
					unsigned int offset, int value)
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
mediatek_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct mtk_gc *rg = to_mediatek_gpio(chip);
	u32 t = mtk_gpio_r32(rg, GPIO_REG_CTRL);

	return (t & BIT(offset)) ? GPIOF_DIR_OUT : GPIOF_DIR_IN;
}

static int
mediatek_gpio_to_irq(struct gpio_chip *chip, unsigned int pin)
{
	struct mtk_data *gpio_data = gpiochip_get_data(chip);
	struct mtk_gc *rg = to_mediatek_gpio(chip);

	return irq_create_mapping(gpio_data->gpio_irq_domain,
				  pin + (rg->bank * MTK_BANK_WIDTH));
}

static int
mediatek_gpio_bank_probe(struct platform_device *pdev, struct device_node *bank)
{
	struct mtk_data *gpio_data = dev_get_drvdata(&pdev->dev);
	const __be32 *id = of_get_property(bank, "reg", NULL);
	struct mtk_gc *rg;
	int ret;

	if (!id || be32_to_cpu(*id) >= MTK_BANK_CNT)
		return -EINVAL;

	rg = &gpio_data->gc_map[be32_to_cpu(*id)];
	memset(rg, 0, sizeof(*rg));

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
	if (gpio_data->gpio_irq_domain)
		rg->chip.to_irq = mediatek_gpio_to_irq;
	rg->bank = be32_to_cpu(*id);

	ret = devm_gpiochip_add_data(&pdev->dev, &rg->chip, gpio_data);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register gpio %d, ret=%d\n",
			rg->chip.ngpio, ret);
		return ret;
	}

	/* set polarity to low for all gpios */
	mtk_gpio_w32(rg, GPIO_REG_POL, 0);

	dev_info(&pdev->dev, "registering %d gpios\n", rg->chip.ngpio);

	return 0;
}

static void
mediatek_gpio_irq_handler(struct irq_desc *desc)
{
	struct mtk_data *gpio_data = irq_desc_get_handler_data(desc);
	int i;

	for (i = 0; i < MTK_BANK_CNT; i++) {
		struct mtk_gc *rg = &gpio_data->gc_map[i];
		unsigned long pending;
		int bit;

		if (!rg)
			continue;

		pending = mtk_gpio_r32(rg, GPIO_REG_STAT);

		for_each_set_bit(bit, &pending, MTK_BANK_WIDTH) {
			u32 map = irq_find_mapping(gpio_data->gpio_irq_domain,
						   (MTK_BANK_WIDTH * i) + bit);

			generic_handle_irq(map);
			mtk_gpio_w32(rg, GPIO_REG_STAT, BIT(bit));
		}
	}
}

static void
mediatek_gpio_irq_unmask(struct irq_data *d)
{
	struct mtk_data *gpio_data = irq_data_get_irq_chip_data(d);
	int pin = d->hwirq;
	int bank = pin / MTK_BANK_WIDTH;
	struct mtk_gc *rg = &gpio_data->gc_map[bank];
	unsigned long flags;
	u32 rise, fall;

	if (!rg)
		return;

	spin_lock_irqsave(&rg->lock, flags);
	rise = mtk_gpio_r32(rg, GPIO_REG_REDGE);
	fall = mtk_gpio_r32(rg, GPIO_REG_FEDGE);
	mtk_gpio_w32(rg, GPIO_REG_REDGE, rise | (PIN_MASK(pin) & rg->rising));
	mtk_gpio_w32(rg, GPIO_REG_FEDGE, fall | (PIN_MASK(pin) & rg->falling));
	spin_unlock_irqrestore(&rg->lock, flags);
}

static void
mediatek_gpio_irq_mask(struct irq_data *d)
{
	struct mtk_data *gpio_data = irq_data_get_irq_chip_data(d);
	int pin = d->hwirq;
	int bank = pin / MTK_BANK_WIDTH;
	struct mtk_gc *rg = &gpio_data->gc_map[bank];
	unsigned long flags;
	u32 rise, fall;

	if (!rg)
		return;

	spin_lock_irqsave(&rg->lock, flags);
	rise = mtk_gpio_r32(rg, GPIO_REG_REDGE);
	fall = mtk_gpio_r32(rg, GPIO_REG_FEDGE);
	mtk_gpio_w32(rg, GPIO_REG_FEDGE, fall & ~PIN_MASK(pin));
	mtk_gpio_w32(rg, GPIO_REG_REDGE, rise & ~PIN_MASK(pin));
	spin_unlock_irqrestore(&rg->lock, flags);
}

static int
mediatek_gpio_irq_type(struct irq_data *d, unsigned int type)
{
	struct mtk_data *gpio_data = irq_data_get_irq_chip_data(d);
	int pin = d->hwirq;
	int bank = pin / MTK_BANK_WIDTH;
	struct mtk_gc *rg = &gpio_data->gc_map[bank];
	u32 mask = PIN_MASK(pin);

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
mediatek_gpio_gpio_map(struct irq_domain *d, unsigned int irq,
		       irq_hw_number_t hw)
{
	int ret;

	ret = irq_set_chip_data(irq, d->host_data);
	if (ret < 0)
		return ret;
	irq_set_chip_and_handler(irq, &mediatek_gpio_irq_chip,
				 handle_level_irq);
	irq_set_handler_data(irq, d);

	return 0;
}

static const struct irq_domain_ops irq_domain_ops = {
	.xlate = irq_domain_xlate_twocell,
	.map = mediatek_gpio_gpio_map,
};

static int
mediatek_gpio_probe(struct platform_device *pdev)
{
	struct device_node *bank, *np = pdev->dev.of_node;
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct mtk_data *gpio_data;

	gpio_data = devm_kzalloc(&pdev->dev, sizeof(*gpio_data), GFP_KERNEL);
	if (!gpio_data)
		return -ENOMEM;

	gpio_data->gpio_membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(gpio_data->gpio_membase))
		return PTR_ERR(gpio_data->gpio_membase);

	gpio_data->gpio_irq = irq_of_parse_and_map(np, 0);
	if (gpio_data->gpio_irq) {
		gpio_data->gpio_irq_domain = irq_domain_add_linear(np,
			MTK_BANK_CNT * MTK_BANK_WIDTH,
			&irq_domain_ops, gpio_data);
		if (!gpio_data->gpio_irq_domain)
			dev_err(&pdev->dev, "irq_domain_add_linear failed\n");
	}

	platform_set_drvdata(pdev, gpio_data);

	for_each_child_of_node(np, bank)
		if (of_device_is_compatible(bank, "mediatek,mt7621-gpio-bank"))
			mediatek_gpio_bank_probe(pdev, bank);

	if (gpio_data->gpio_irq_domain)
		irq_set_chained_handler_and_data(gpio_data->gpio_irq,
						 mediatek_gpio_irq_handler,
						 gpio_data);

	return 0;
}

static const struct of_device_id mediatek_gpio_match[] = {
	{ .compatible = "mediatek,mt7621-gpio" },
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

module_platform_driver(mediatek_gpio_driver);
