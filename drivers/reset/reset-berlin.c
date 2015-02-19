/*
 * Copyright (C) 2014 Marvell Technology Group Ltd.
 *
 * Antoine Tenart <antoine.tenart@free-electrons.com>
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/types.h>

#define BERLIN_MAX_RESETS	32

#define to_berlin_reset_priv(p)		\
	container_of((p), struct berlin_reset_priv, rcdev)

struct berlin_reset_priv {
	void __iomem			*base;
	unsigned int			size;
	struct reset_controller_dev	rcdev;
};

static int berlin_reset_reset(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct berlin_reset_priv *priv = to_berlin_reset_priv(rcdev);
	int offset = id >> 8;
	int mask = BIT(id & 0x1f);

	writel(mask, priv->base + offset);

	/* let the reset be effective */
	udelay(10);

	return 0;
}

static struct reset_control_ops berlin_reset_ops = {
	.reset	= berlin_reset_reset,
};

static int berlin_reset_xlate(struct reset_controller_dev *rcdev,
			      const struct of_phandle_args *reset_spec)
{
	struct berlin_reset_priv *priv = to_berlin_reset_priv(rcdev);
	unsigned offset, bit;

	if (WARN_ON(reset_spec->args_count != rcdev->of_reset_n_cells))
		return -EINVAL;

	offset = reset_spec->args[0];
	bit = reset_spec->args[1];

	if (offset >= priv->size)
		return -EINVAL;

	if (bit >= BERLIN_MAX_RESETS)
		return -EINVAL;

	return (offset << 8) | bit;
}

static int __berlin_reset_init(struct device_node *np)
{
	struct berlin_reset_priv *priv;
	struct resource res;
	resource_size_t size;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = of_address_to_resource(np, 0, &res);
	if (ret)
		goto err;

	size = resource_size(&res);
	priv->base = ioremap(res.start, size);
	if (!priv->base) {
		ret = -ENOMEM;
		goto err;
	}
	priv->size = size;

	priv->rcdev.owner = THIS_MODULE;
	priv->rcdev.ops = &berlin_reset_ops;
	priv->rcdev.of_node = np;
	priv->rcdev.of_reset_n_cells = 2;
	priv->rcdev.of_xlate = berlin_reset_xlate;

	reset_controller_register(&priv->rcdev);

	return 0;

err:
	kfree(priv);
	return ret;
}

static const struct of_device_id berlin_reset_of_match[] __initconst = {
	{ .compatible = "marvell,berlin2-chip-ctrl" },
	{ .compatible = "marvell,berlin2cd-chip-ctrl" },
	{ .compatible = "marvell,berlin2q-chip-ctrl" },
	{ },
};

static int __init berlin_reset_init(void)
{
	struct device_node *np;
	int ret;

	for_each_matching_node(np, berlin_reset_of_match) {
		ret = __berlin_reset_init(np);
		if (ret)
			return ret;
	}

	return 0;
}
arch_initcall(berlin_reset_init);
