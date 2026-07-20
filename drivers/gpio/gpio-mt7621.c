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
	struct gpio_generic_chip chip;
	struct mtk *parent_priv;
	int bank;
	u32 rising;
	u32 falling;
	u32 hlevel;
	u32 llevel;
};

/**
 * struct mtk - state container for
 * data of the platform driver. It is 3
 * separate gpio-chip having an IRQ
 * linear domain shared for all of them
 * @pdev: platform device instance
 * @base: memory base address
 * @irq_domain: IRQ linear domain shared across the three gpio chips
 * @gpio_irq: irq number from the device tree
 * @num_gpios: total number of gpio pins on the three gpio chips
 * @gc_map: array of the gpio chips
 */
struct mtk {
	struct platform_device *pdev;
	void __iomem *base;
	struct irq_domain *irq_domain;
	int gpio_irq;
	int num_gpios;
	struct mtk_gc gc_map[MTK_BANK_CNT];
};

static inline struct mtk *
mt7621_gpio_gc_to_priv(struct gpio_chip *gc)
{
	struct mtk_gc *bank = gpiochip_get_data(gc);

	return bank->parent_priv;
}

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
	struct mtk *mtk = mt7621_gpio_gc_to_priv(gc);

	offset = (rg->bank * GPIO_BANK_STRIDE) + offset;
	gpio_generic_write_reg(&rg->chip, mtk->base + offset, val);
}

static inline u32
mtk_gpio_r32(struct mtk_gc *rg, u32 offset)
{
	struct gpio_chip *gc = &rg->chip.gc;
	struct mtk *mtk = mt7621_gpio_gc_to_priv(gc);

	offset = (rg->bank * GPIO_BANK_STRIDE) + offset;
	return gpio_generic_read_reg(&rg->chip, mtk->base + offset);
}

static void
mt7621_gpio_irq_bank_handler(struct mtk_gc *bank)
{
	struct mtk *priv = bank->parent_priv;
	struct irq_domain *domain = priv->irq_domain;
	int hwbase = bank->chip.gc.offset;
	unsigned long pending;
	unsigned int offset;

	pending = mtk_gpio_r32(bank, GPIO_REG_STAT);
	if (!pending)
		return;

	mtk_gpio_w32(bank, GPIO_REG_STAT, pending);

	for_each_set_bit(offset, &pending, MTK_BANK_WIDTH)
		generic_handle_domain_irq(domain, hwbase + offset);
}

static void
mt7621_gpio_irq_handler(struct irq_desc *desc)
{
	struct mtk *priv = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	int i;

	chained_irq_enter(chip, desc);
	for (i = 0; i < MTK_BANK_CNT; i++) {
		struct mtk_gc *bank = &priv->gc_map[i];

		mt7621_gpio_irq_bank_handler(bank);
	}
	chained_irq_exit(chip, desc);
}

static int
mt7621_gpio_hwirq_to_offset(irq_hw_number_t hwirq, struct mtk_gc *bank)
{
	return hwirq - bank->chip.gc.offset;
}

static void
mediatek_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct mtk_gc *rg = gpiochip_get_data(gc);
	u32 mask = mt7621_gpio_hwirq_to_offset(d->hwirq, rg);
	u32 rise, fall, high, low;

	gpiochip_enable_irq(gc, mask);

	guard(gpio_generic_lock_irqsave)(&rg->chip);

	rise = mtk_gpio_r32(rg, GPIO_REG_REDGE);
	fall = mtk_gpio_r32(rg, GPIO_REG_FEDGE);
	high = mtk_gpio_r32(rg, GPIO_REG_HLVL);
	low = mtk_gpio_r32(rg, GPIO_REG_LLVL);
	mtk_gpio_w32(rg, GPIO_REG_REDGE, rise | (BIT(mask) & rg->rising));
	mtk_gpio_w32(rg, GPIO_REG_FEDGE, fall | (BIT(mask) & rg->falling));
	mtk_gpio_w32(rg, GPIO_REG_HLVL, high | (BIT(mask) & rg->hlevel));
	mtk_gpio_w32(rg, GPIO_REG_LLVL, low | (BIT(mask) & rg->llevel));
}

static void
mediatek_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct mtk_gc *rg = gpiochip_get_data(gc);
	u32 mask = mt7621_gpio_hwirq_to_offset(d->hwirq, rg);
	u32 rise, fall, high, low;

	scoped_guard(gpio_generic_lock_irqsave, &rg->chip) {
		rise = mtk_gpio_r32(rg, GPIO_REG_REDGE);
		fall = mtk_gpio_r32(rg, GPIO_REG_FEDGE);
		high = mtk_gpio_r32(rg, GPIO_REG_HLVL);
		low = mtk_gpio_r32(rg, GPIO_REG_LLVL);
		mtk_gpio_w32(rg, GPIO_REG_FEDGE, fall & ~BIT(mask));
		mtk_gpio_w32(rg, GPIO_REG_REDGE, rise & ~BIT(mask));
		mtk_gpio_w32(rg, GPIO_REG_HLVL, high & ~BIT(mask));
		mtk_gpio_w32(rg, GPIO_REG_LLVL, low & ~BIT(mask));
	}

	gpiochip_disable_irq(gc, mask);
}

static int
mediatek_gpio_irq_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct mtk_gc *rg = gpiochip_get_data(gc);
	u32 mask = BIT(mt7621_gpio_hwirq_to_offset(d->hwirq, rg));

	guard(gpio_generic_lock_irqsave)(&rg->chip);

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
mt7621_gpio_irq_reqres(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct mtk_gc *rg = gpiochip_get_data(gc);
	unsigned int irq = mt7621_gpio_hwirq_to_offset(d->hwirq, rg);

	return gpiochip_reqres_irq(gc, irq);
}

static void
mt7621_gpio_irq_relres(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct mtk_gc *rg = gpiochip_get_data(gc);
	unsigned int irq = mt7621_gpio_hwirq_to_offset(d->hwirq, rg);

	gpiochip_relres_irq(gc, irq);
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
	.irq_request_resources = mt7621_gpio_irq_reqres,
	.irq_release_resources = mt7621_gpio_irq_relres,
	.irq_mask_ack	= mediatek_gpio_irq_mask,
	.irq_mask	= mediatek_gpio_irq_mask,
	.irq_unmask	= mediatek_gpio_irq_unmask,
	.irq_set_type	= mediatek_gpio_irq_type,
	.flags		= IRQCHIP_IMMUTABLE,
};

static void
mt7621_gpio_remove(void *data)
{
	struct mtk *priv = data;
	int offset, virq;

	if (priv->gpio_irq > 0)
		irq_set_chained_handler_and_data(priv->gpio_irq, NULL, NULL);

	/* Remove all IRQ mappings and delete the domain */
	if (priv->irq_domain) {
		for (offset = 0; offset < priv->num_gpios; offset++) {
			virq = irq_find_mapping(priv->irq_domain, offset);
			irq_dispose_mapping(virq);
		}
		irq_domain_remove(priv->irq_domain);
	}
}

static struct mtk_gc *
mt7621_gpio_hwirq_to_bank(struct mtk *priv, irq_hw_number_t hwirq)
{
	int i;

	for (i = 0; i < MTK_BANK_CNT; i++) {
		struct mtk_gc *bank = &priv->gc_map[i];

		if (hwirq >= bank->chip.gc.offset &&
		    hwirq < (bank->chip.gc.offset + bank->chip.gc.ngpio))
			return bank;
	}

	return NULL;
}

static int
mt7621_gpio_irq_map(struct irq_domain *d, unsigned int irq,
		    irq_hw_number_t hwirq)
{
	struct mtk *priv = d->host_data;
	struct mtk_gc *bank = mt7621_gpio_hwirq_to_bank(priv, hwirq);
	struct platform_device *pdev = priv->pdev;
	int ret;

	if (!bank)
		return -EINVAL;

	dev_dbg(&pdev->dev, "Mapping irq %d for gpio line %d (bank %d)\n",
		irq, (int)hwirq, bank->bank);

	ret = irq_set_chip_data(irq, &bank->chip.gc);
	if (ret < 0)
		return ret;

	irq_set_chip_and_handler(irq, &mt7621_irq_chip, handle_simple_irq);
	irq_set_noprobe(irq);

	return 0;
}

static void
mt7621_gpio_irq_unmap(struct irq_domain *d, unsigned int irq)
{
	irq_set_chip_and_handler(irq, NULL, NULL);
	irq_set_chip_data(irq, NULL);
}

static const struct irq_domain_ops mt7621_gpio_irq_domain_ops = {
	.map = mt7621_gpio_irq_map,
	.unmap = mt7621_gpio_irq_unmap,
	.xlate = irq_domain_xlate_twocell,
};

static int
mt7621_gpio_irq_setup(struct platform_device *pdev,
		      struct mtk *priv)
{
	struct device *dev = &pdev->dev;

	priv->irq_domain = irq_domain_create_linear(dev_fwnode(dev),
						    priv->num_gpios,
						    &mt7621_gpio_irq_domain_ops,
						    priv);
	if (!priv->irq_domain) {
		dev_err(dev, "Couldn't allocate IRQ domain\n");
		return -ENXIO;
	}

	irq_set_chained_handler_and_data(priv->gpio_irq,
					 mt7621_gpio_irq_handler, priv);
	irq_set_status_flags(priv->gpio_irq, IRQ_DISABLE_UNLAZY);

	return 0;
}

static int
mt7621_gpio_to_irq(struct gpio_chip *gc, unsigned int offset)
{
	struct mtk *priv = mt7621_gpio_gc_to_priv(gc);
	/* gc_offset is relative to this gpio_chip; want real offset */
	int hwirq = offset + gc->offset;

	if (hwirq >= priv->num_gpios)
		return -ENXIO;

	return irq_create_mapping(priv->irq_domain, hwirq);
}

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

	rg->parent_priv = mtk;
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
	rg->chip.gc.ngpio = MTK_BANK_WIDTH;
	rg->chip.gc.label = devm_kasprintf(dev, GFP_KERNEL, "%s-bank%d",
					dev_name(dev), bank);
	if (!rg->chip.gc.label)
		return -ENOMEM;

	rg->chip.gc.offset = bank * MTK_BANK_WIDTH;
	if (mtk->gpio_irq > 0)
		rg->chip.gc.to_irq = mt7621_gpio_to_irq;

	ret = devm_gpiochip_add_data(dev, &rg->chip.gc, rg);
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

	mtk->pdev = pdev;
	mtk->num_gpios = MTK_BANK_WIDTH * MTK_BANK_CNT;
	platform_set_drvdata(pdev, mtk);

	if (mtk->gpio_irq > 0) {
		ret = mt7621_gpio_irq_setup(pdev, mtk);
		if (ret)
			return ret;
	}

	ret = devm_add_action_or_reset(dev, mt7621_gpio_remove, mtk);
	if (ret)
		return ret;

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
