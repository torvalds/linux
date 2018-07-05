// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2009-2011 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2013 John Crispin <blogic@openwrt.org>
 */

#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

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
	struct gpio_chip chip;
	spinlock_t lock;
	int bank;
	u32 rising;
	u32 falling;
	u32 hlevel;
	u32 llevel;
};

/**
 * struct mtk_data - state container for
 * data of the platform driver. It is 3
 * separate gpio-chip each one with its
 * own irq_chip.
 * @dev: device instance
 * @gpio_membase: memory base address
 * @gpio_irq: irq number from the device tree
 * @gc_map: array of the gpio chips
 */
struct mtk_data {
	struct device *dev;
	void __iomem *gpio_membase;
	int gpio_irq;
	struct mtk_gc gc_map[MTK_BANK_CNT];
};

static inline struct mtk_gc *
to_mediatek_gpio(struct gpio_chip *chip)
{
	return container_of(chip, struct mtk_gc, chip);
}

static inline void
mtk_gpio_w32(struct mtk_gc *rg, u32 offset, u32 val)
{
	struct gpio_chip *gc = &rg->chip;
	struct mtk_data *gpio_data = gpiochip_get_data(gc);

	offset = (rg->bank * GPIO_BANK_STRIDE) + offset;
	gc->write_reg(gpio_data->gpio_membase + offset, val);
}

static inline u32
mtk_gpio_r32(struct mtk_gc *rg, u32 offset)
{
	struct gpio_chip *gc = &rg->chip;
	struct mtk_data *gpio_data = gpiochip_get_data(gc);

	offset = (rg->bank * GPIO_BANK_STRIDE) + offset;
	return gc->read_reg(gpio_data->gpio_membase + offset);
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
		u32 map = irq_find_mapping(gc->irq.domain, bit);

		generic_handle_irq(map);
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
	unsigned long flags;
	u32 rise, fall, high, low;

	spin_lock_irqsave(&rg->lock, flags);
	rise = mtk_gpio_r32(rg, GPIO_REG_REDGE);
	fall = mtk_gpio_r32(rg, GPIO_REG_FEDGE);
	high = mtk_gpio_r32(rg, GPIO_REG_HLVL);
	low = mtk_gpio_r32(rg, GPIO_REG_LLVL);
	mtk_gpio_w32(rg, GPIO_REG_REDGE, rise | (BIT(pin) & rg->rising));
	mtk_gpio_w32(rg, GPIO_REG_FEDGE, fall | (BIT(pin) & rg->falling));
	mtk_gpio_w32(rg, GPIO_REG_HLVL, high | (BIT(pin) & rg->hlevel));
	mtk_gpio_w32(rg, GPIO_REG_LLVL, low | (BIT(pin) & rg->llevel));
	spin_unlock_irqrestore(&rg->lock, flags);
}

static void
mediatek_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct mtk_gc *rg = to_mediatek_gpio(gc);
	int pin = d->hwirq;
	unsigned long flags;
	u32 rise, fall, high, low;

	spin_lock_irqsave(&rg->lock, flags);
	rise = mtk_gpio_r32(rg, GPIO_REG_REDGE);
	fall = mtk_gpio_r32(rg, GPIO_REG_FEDGE);
	high = mtk_gpio_r32(rg, GPIO_REG_HLVL);
	low = mtk_gpio_r32(rg, GPIO_REG_LLVL);
	mtk_gpio_w32(rg, GPIO_REG_FEDGE, fall & ~BIT(pin));
	mtk_gpio_w32(rg, GPIO_REG_REDGE, rise & ~BIT(pin));
	mtk_gpio_w32(rg, GPIO_REG_HLVL, high & ~BIT(pin));
	mtk_gpio_w32(rg, GPIO_REG_LLVL, low & ~BIT(pin));
	spin_unlock_irqrestore(&rg->lock, flags);
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

static struct irq_chip mediatek_gpio_irq_chip = {
	.irq_unmask		= mediatek_gpio_irq_unmask,
	.irq_mask		= mediatek_gpio_irq_mask,
	.irq_mask_ack		= mediatek_gpio_irq_mask,
	.irq_set_type		= mediatek_gpio_irq_type,
};

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

static int
mediatek_gpio_bank_probe(struct platform_device *pdev,
			 struct device_node *node, int bank)
{
	struct mtk_data *gpio = dev_get_drvdata(&pdev->dev);
	struct mtk_gc *rg;
	void __iomem *dat, *set, *ctrl, *diro;
	int ret;

	rg = &gpio->gc_map[bank];
	memset(rg, 0, sizeof(*rg));

	spin_lock_init(&rg->lock);
	rg->chip.of_node = node;
	rg->bank = bank;

	dat = gpio->gpio_membase + GPIO_REG_DATA +
		(rg->bank * GPIO_BANK_STRIDE);
	set = gpio->gpio_membase + GPIO_REG_DSET +
		(rg->bank * GPIO_BANK_STRIDE);
	ctrl = gpio->gpio_membase + GPIO_REG_DCLR +
		(rg->bank * GPIO_BANK_STRIDE);
	diro = gpio->gpio_membase + GPIO_REG_CTRL +
		(rg->bank * GPIO_BANK_STRIDE);

	ret = bgpio_init(&rg->chip, &pdev->dev, 4,
			 dat, set, ctrl, diro, NULL, 0);
	if (ret) {
		dev_err(&pdev->dev, "bgpio_init() failed\n");
		return ret;
	}

	rg->chip.of_gpio_n_cells = 2;
	rg->chip.of_xlate = mediatek_gpio_xlate;
	rg->chip.label = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s-bank%d",
					dev_name(&pdev->dev), bank);

	ret = devm_gpiochip_add_data(&pdev->dev, &rg->chip, gpio);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register gpio %d, ret=%d\n",
			rg->chip.ngpio, ret);
		return ret;
	}

	if (gpio->gpio_irq) {
		/*
		 * Manually request the irq here instead of passing
		 * a flow-handler to gpiochip_set_chained_irqchip,
		 * because the irq is shared.
		 */
		ret = devm_request_irq(&pdev->dev, gpio->gpio_irq,
				       mediatek_gpio_irq_handler, IRQF_SHARED,
				       rg->chip.label, &rg->chip);

		if (ret) {
			dev_err(&pdev->dev, "Error requesting IRQ %d: %d\n",
				gpio->gpio_irq, ret);
			return ret;
		}

		ret = gpiochip_irqchip_add(&rg->chip, &mediatek_gpio_irq_chip,
					   0, handle_simple_irq, IRQ_TYPE_NONE);
		if (ret) {
			dev_err(&pdev->dev, "failed to add gpiochip_irqchip\n");
			return ret;
		}

		gpiochip_set_chained_irqchip(&rg->chip, &mediatek_gpio_irq_chip,
					     gpio->gpio_irq, NULL);
	}

	/* set polarity to low for all gpios */
	mtk_gpio_w32(rg, GPIO_REG_POL, 0);

	dev_info(&pdev->dev, "registering %d gpios\n", rg->chip.ngpio);

	return 0;
}

static int
mediatek_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct mtk_data *gpio_data;
	int i;

	gpio_data = devm_kzalloc(&pdev->dev, sizeof(*gpio_data), GFP_KERNEL);
	if (!gpio_data)
		return -ENOMEM;

	gpio_data->gpio_membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(gpio_data->gpio_membase))
		return PTR_ERR(gpio_data->gpio_membase);

	gpio_data->gpio_irq = irq_of_parse_and_map(np, 0);
	gpio_data->dev = &pdev->dev;
	platform_set_drvdata(pdev, gpio_data);
	mediatek_gpio_irq_chip.name = dev_name(&pdev->dev);

	for (i = 0; i < MTK_BANK_CNT; i++)
		mediatek_gpio_bank_probe(pdev, np, i);

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
