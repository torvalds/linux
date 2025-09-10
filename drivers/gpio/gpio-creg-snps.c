// SPDX-License-Identifier: GPL-2.0+
//
// Synopsys CREG (Control REGisters) GPIO driver
//
// Copyright (C) 2018 Synopsys
// Author: Eugeniy Paltsev <Eugeniy.Paltsev@synopsys.com>

#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define MAX_GPIO	32

struct creg_layout {
	u8 ngpio;
	u8 shift[MAX_GPIO];
	u8 on[MAX_GPIO];
	u8 off[MAX_GPIO];
	u8 bit_per_gpio[MAX_GPIO];
};

struct creg_gpio {
	struct gpio_chip gc;
	void __iomem *regs;
	spinlock_t lock;
	const struct creg_layout *layout;
};

static int creg_gpio_set(struct gpio_chip *gc, unsigned int offset, int val)
{
	struct creg_gpio *hcg = gpiochip_get_data(gc);
	const struct creg_layout *layout = hcg->layout;
	u32 reg, reg_shift, value;
	unsigned long flags;
	int i;

	value = val ? hcg->layout->on[offset] : hcg->layout->off[offset];

	reg_shift = layout->shift[offset];
	for (i = 0; i < offset; i++)
		reg_shift += layout->bit_per_gpio[i] + layout->shift[i];

	spin_lock_irqsave(&hcg->lock, flags);
	reg = readl(hcg->regs);
	reg &= ~(GENMASK(layout->bit_per_gpio[i] - 1, 0) << reg_shift);
	reg |=  (value << reg_shift);
	writel(reg, hcg->regs);
	spin_unlock_irqrestore(&hcg->lock, flags);

	return 0;
}

static int creg_gpio_dir_out(struct gpio_chip *gc, unsigned int offset, int val)
{
	return creg_gpio_set(gc, offset, val);
}

static int creg_gpio_validate_pg(struct device *dev, struct creg_gpio *hcg,
				 int i)
{
	const struct creg_layout *layout = hcg->layout;

	if (layout->bit_per_gpio[i] < 1 || layout->bit_per_gpio[i] > 8)
		return -EINVAL;

	/* Check that on value fits its placeholder */
	if (GENMASK(31, layout->bit_per_gpio[i]) & layout->on[i])
		return -EINVAL;

	/* Check that off value fits its placeholder */
	if (GENMASK(31, layout->bit_per_gpio[i]) & layout->off[i])
		return -EINVAL;

	if (layout->on[i] == layout->off[i])
		return -EINVAL;

	return 0;
}

static int creg_gpio_validate(struct device *dev, struct creg_gpio *hcg,
			      u32 ngpios)
{
	u32 reg_len = 0;
	int i;

	if (hcg->layout->ngpio < 1 || hcg->layout->ngpio > MAX_GPIO)
		return -EINVAL;

	if (ngpios < 1 || ngpios > hcg->layout->ngpio) {
		dev_err(dev, "ngpios must be in [1:%u]\n", hcg->layout->ngpio);
		return -EINVAL;
	}

	for (i = 0; i < hcg->layout->ngpio; i++) {
		if (creg_gpio_validate_pg(dev, hcg, i))
			return -EINVAL;

		reg_len += hcg->layout->shift[i] + hcg->layout->bit_per_gpio[i];
	}

	/* Check that we fit in 32 bit register */
	if (reg_len > 32)
		return -EINVAL;

	return 0;
}

static const struct creg_layout hsdk_cs_ctl = {
	.ngpio		= 10,
	.shift		= { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	.off		= { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 },
	.on		= { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 },
	.bit_per_gpio	= { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 }
};

static const struct creg_layout axs10x_flsh_cs_ctl = {
	.ngpio		= 1,
	.shift		= { 0 },
	.off		= { 1 },
	.on		= { 3 },
	.bit_per_gpio	= { 2 }
};

static const struct of_device_id creg_gpio_ids[] = {
	{
		.compatible = "snps,creg-gpio-axs10x",
		.data = &axs10x_flsh_cs_ctl
	}, {
		.compatible = "snps,creg-gpio-hsdk",
		.data = &hsdk_cs_ctl
	}, { /* sentinel */ }
};

static int creg_gpio_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct creg_gpio *hcg;
	u32 ngpios;
	int ret;

	hcg = devm_kzalloc(dev, sizeof(struct creg_gpio), GFP_KERNEL);
	if (!hcg)
		return -ENOMEM;

	hcg->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hcg->regs))
		return PTR_ERR(hcg->regs);

	match = of_match_node(creg_gpio_ids, pdev->dev.of_node);
	hcg->layout = match->data;
	if (!hcg->layout)
		return -EINVAL;

	ret = of_property_read_u32(dev->of_node, "ngpios", &ngpios);
	if (ret)
		return ret;

	ret = creg_gpio_validate(dev, hcg, ngpios);
	if (ret)
		return ret;

	spin_lock_init(&hcg->lock);

	hcg->gc.parent = dev;
	hcg->gc.label = dev_name(dev);
	hcg->gc.base = -1;
	hcg->gc.ngpio = ngpios;
	hcg->gc.set = creg_gpio_set;
	hcg->gc.direction_output = creg_gpio_dir_out;

	ret = devm_gpiochip_add_data(dev, &hcg->gc, hcg);
	if (ret)
		return ret;

	dev_info(dev, "GPIO controller with %d gpios probed\n", ngpios);

	return 0;
}

static struct platform_driver creg_gpio_snps_driver = {
	.driver = {
		.name = "snps-creg-gpio",
		.of_match_table = creg_gpio_ids,
	},
	.probe  = creg_gpio_probe,
};
builtin_platform_driver(creg_gpio_snps_driver);
