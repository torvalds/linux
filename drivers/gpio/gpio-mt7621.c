// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2009-2011 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2013 John Crispin <blogic@openwrt.org>
 */

#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/generic.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define MTK_BANK_CNT	3
#define MTK_BANK_WIDTH	32

#define GPIO_BANK_STRIDE	0x04
#define GPIO_REG_CTRL		0x00
#define GPIO_REG_POL		0x10
#define GPIO_REG_DATA		0x20
#define GPIO_REG_DSET		0x30
#define GPIO_REG_DCLR		0x40
#define GPIO_REG_REDGE		0x50
#define GPIO_REG_FEDGE		0x60
#define GPIO_REG_HLVL		0x70
#define GPIO_REG_LLVL		0x80
#define GPIO_REG_STAT		0x90
#define GPIO_REG_EDGE		0xA0

struct mtk_gc {
	struct irq_chip irq_chip;
	struct gpio_generic_chip chip;
	int bank;
	u32 rising;
	u32 falling;
	u32 hlevel;
	u32 llevel;
};

/**
 * struct mtk - state container for
 * data of the platform driver. It is 3
 * separate gpio-chip each one with its
 * own irq_chip.
 * @dev: device instance
 * @base: memory base address
 * @gpio_irq: irq number from the device tree
 * @gc_map: array of the gpio chips
 */
struct mtk {
	struct device *dev;
	void __iomem *base;
	int gpio_irq;
	struct mtk_gc gc_map[MTK_BANK_CNT];
};

static inline struct mtk_gc *
to_mediatek_gpio(struct gpio_chip *chip)
{
	struct gpio_generic_chip *gen_gc = to_gpio_generic_chip(chip);

	return container_of(gen_gc, struct mtk_gc, chip);
}

static inline void
mtk_gpio_w32(struct mtk_gc *rg, u32 offset, u32 val)
{
	struct gpio_chip *gc = &rg->chip.gc;
	struct mtk *mtk = gpiochip_get_data(gc);

	offset = (rg->bank * GPIO_BANK_STRIDE) + offset;
	gpio_generic_write_reg(&rg->chip, mtk->base + offset, val);
}

static inline u32
mtk_gpio_r32(struct mtk_gc *rg, u32 offset)
{
	struct gpio_chip *gc = &rg->chip.gc;
	struct mtk *mtk = gpiochip_get_data(gc);

	offset = (rg->bank * GPIO_BANK_STRIDE) + offset;
	return gpio_generic_read_reg(&rg->chip, mtk->base + offset);
}

static irqreturn_t
mediatek_gpio_irq_handler(int irq, void *data)
{
	struct gpio_chip *gc = data;
	struct mtk_gc *rg = to_mediatek_gpio(gc);
	irqreturn_t ret = IRQ_NONE;
	unsigned long pending;
	int bit;

	pending = mtk_gpio_r32(rg, GPIO_REG_STAT);

	for_each_set_bit(bit, &pending, MTK_BANK_WIDTH) {
		generic_handle_domain_irq(gc->irq.domain, bit);
		mtk_gpio_w32(rg, GPIO_REG_STAT, BIT(bit));
		ret |= IRQ_HANDLED;
	}

	return ret;
}

static void
mediatek_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct mtk_gc *rg = to_mediatek_gpio(gc);
	int pin = d->hwirq;
	u32 rise, fall, high, low;

	gpiochip_enable_irq(gc, d->hwirq);

	guard(gpio_generic_lock_irqsave)(&rg->chip);

	rise = mtk_gpio_r32(rg, GPIO_REG_REDGE);
	fall = mtk_gpio_r32(rg, GPIO_REG_FEDGE);
	high = mtk_gpio_r32(rg, GPIO_REG_HLVL);
	low = mtk_gpio_r32(rg, GPIO_REG_LLVL);
	mtk_gpio_w32(rg, GPIO_REG_REDGE, rise | (BIT(pin) & rg->rising));
	mtk_gpio_w32(rg, GPIO_REG_FEDGE, fall | (BIT(pin) & rg->falling));
	mtk_gpio_w32(rg, GPIO_REG_HLVL, high | (BIT(pin) & rg->hlevel));
	mtk_gpio_w32(rg, GPIO_REG_LLVL, low | (BIT(pin) & rg->llevel));
}

static void
mediatek_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct mtk_gc *rg = to_mediatek_gpio(gc);
	int pin = d->hwirq;
	u32 rise, fall, high, low;

	scoped_guard(gpio_generic_lock_irqsave, &rg->chip) {
		rise = mtk_gpio_r32(rg, GPIO_REG_REDGE);
		fall = mtk_gpio_r32(rg, GPIO_REG_FEDGE);
		high = mtk_gpio_r32(rg, GPIO_REG_HLVL);
		low = mtk_gpio_r32(rg, GPIO_REG_LLVL);
		mtk_gpio_w32(rg, GPIO_REG_FEDGE, fall & ~BIT(pin));
		mtk_gpio_w32(rg, GPIO_REG_REDGE, rise & ~BIT(pin));
		mtk_gpio_w32(rg, GPIO_REG_HLVL, high & ~BIT(pin));
		mtk_gpio_w32(rg, GPIO_REG_LLVL, low & ~BIT(pin));
	}

	gpiochip_disable_irq(gc, d->hwirq);
}

static int
mediatek_gpio_irq_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct mtk_gc *rg = to_mediatek_gpio(gc);
	int pin = d->hwirq;
	u32 mask = BIT(pin);

	if (type == IRQ_TYPE_PROBE) {
		if ((rg->rising | rg->falling |
		     rg->hlevel | rg->llevel) & mask)
			return 0;

		type = IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING;
	}

	rg->rising &= ~mask;
	rg->falling &= ~mask;
	rg->hlevel &= ~mask;
	rg->llevel &= ~mask;

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_BOTH:
		rg->rising |= mask;
		rg->falling |= mask;
		break;
	case IRQ_TYPE_EDGE_RISING:
		rg->rising |= mask;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		rg->falling |= mask;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		rg->hlevel |= mask;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		rg->llevel |= mask;
		break;
	}

	return 0;
}

static int
mediatek_gpio_xlate(struct gpio_chip *chip,
		    const struct of_phandle_args *spec, u32 *flags)
{
	int gpio = spec->args[0];
	struct mtk_gc *rg = to_mediatek_gpio(chip);

	if (rg->bank != gpio / MTK_BANK_WIDTH)
		return -EINVAL;

	if (flags)
		*flags = spec->args[1];

	return gpio % MTK_BANK_WIDTH;
}

static const struct irq_chip mt7621_irq_chip = {
	.name		= "mt7621-gpio",
	.irq_mask_ack	= mediatek_gpio_irq_mask,
	.irq_mask	= mediatek_gpio_irq_mask,
	.irq_unmask	= mediatek_gpio_irq_unmask,
	.irq_set_type	= mediatek_gpio_irq_type,
	.flags		= IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static int
mediatek_gpio_bank_probe(struct device *dev, int bank)
{
	struct gpio_generic_chip_config config;
	struct mtk *mtk = dev_get_drvdata(dev);
	struct mtk_gc *rg;
	void __iomem *dat, *set, *ctrl, *diro;
	int ret;

	rg = &mtk->gc_map[bank];
	memset(rg, 0, sizeof(*rg));

	rg->bank = bank;

	dat = mtk->base + GPIO_REG_DATA + (rg->bank * GPIO_BANK_STRIDE);
	set = mtk->base + GPIO_REG_DSET + (rg->bank * GPIO_BANK_STRIDE);
	ctrl = mtk->base + GPIO_REG_DCLR + (rg->bank * GPIO_BANK_STRIDE);
	diro = mtk->base + GPIO_REG_CTRL + (rg->bank * GPIO_BANK_STRIDE);

	config = (struct gpio_generic_chip_config) {
		.dev = dev,
		.sz = 4,
		.dat = dat,
		.set = set,
		.clr = ctrl,
		.dirout = diro,
		.flags = GPIO_GENERIC_NO_SET_ON_INPUT,
	};

	ret = gpio_generic_chip_init(&rg->chip, &config);
	if (ret) {
		dev_err(dev, "failed to initialize generic GPIO chip\n");
		return ret;
	}

	rg->chip.gc.of_gpio_n_cells = 2;
	rg->chip.gc.of_xlate = mediatek_gpio_xlate;
	rg->chip.gc.label = devm_kasprintf(dev, GFP_KERNEL, "%s-bank%d",
					dev_name(dev), bank);
	if (!rg->chip.gc.label)
		return -ENOMEM;

	rg->chip.gc.offset = bank * MTK_BANK_WIDTH;

	if (mtk->gpio_irq) {
		struct gpio_irq_chip *girq;

		/*
		 * Directly request the irq here instead of passing
		 * a flow-handler because the irq is shared.
		 */
		ret = devm_request_irq(dev, mtk->gpio_irq,
				       mediatek_gpio_irq_handler, IRQF_SHARED,
				       rg->chip.gc.label, &rg->chip.gc);

		if (ret) {
			dev_err(dev, "Error requesting IRQ %d: %d\n",
				mtk->gpio_irq, ret);
			return ret;
		}

		girq = &rg->chip.gc.irq;
		gpio_irq_chip_set_chip(girq, &mt7621_irq_chip);
		/* This will let us handle the parent IRQ in the driver */
		girq->parent_handler = NULL;
		girq->num_parents = 0;
		girq->parents = NULL;
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_simple_irq;
	}

	ret = devm_gpiochip_add_data(dev, &rg->chip.gc, mtk);
	if (ret < 0) {
		dev_err(dev, "Could not register gpio %d, ret=%d\n",
			rg->chip.gc.ngpio, ret);
		return ret;
	}

	/* set polarity to low for all gpios */
	mtk_gpio_w32(rg, GPIO_REG_POL, 0);

	dev_info(dev, "registering %d gpios\n", rg->chip.gc.ngpio);

	return 0;
}

static int
mediatek_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk *mtk;
	int i;
	int ret;

	mtk = devm_kzalloc(dev, sizeof(*mtk), GFP_KERNEL);
	if (!mtk)
		return -ENOMEM;

	mtk->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mtk->base))
		return PTR_ERR(mtk->base);

	mtk->gpio_irq = platform_get_irq(pdev, 0);
	if (mtk->gpio_irq < 0)
		return mtk->gpio_irq;

	mtk->dev = dev;
	platform_set_drvdata(pdev, mtk);

	for (i = 0; i < MTK_BANK_CNT; i++) {
		ret = mediatek_gpio_bank_probe(dev, i);
		if (ret)
			return ret;
	}

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

builtin_platform_driver(mediatek_gpio_driver);
