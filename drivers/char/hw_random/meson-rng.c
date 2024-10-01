// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (c) 2016 BayLibre, SAS.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2014 Amlogic, Inc.
 */
#include <linux/err.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/hw_random.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/iopoll.h>

#define RNG_DATA	0x00
#define RNG_S4_DATA	0x08
#define RNG_S4_CFG	0x00

#define RUN_BIT		BIT(0)
#define SEED_READY_STS_BIT	BIT(31)

struct meson_rng_priv {
	int (*read)(struct hwrng *rng, void *buf, size_t max, bool wait);
};

struct meson_rng_data {
	void __iomem *base;
	struct hwrng rng;
	struct device *dev;
};

static int meson_rng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct meson_rng_data *data =
			container_of(rng, struct meson_rng_data, rng);

	*(u32 *)buf = readl_relaxed(data->base + RNG_DATA);

	return sizeof(u32);
}

static int meson_rng_wait_status(void __iomem *cfg_addr, int bit)
{
	u32 status = 0;
	int ret;

	ret = readl_relaxed_poll_timeout_atomic(cfg_addr,
						status, !(status & bit),
						10, 10000);
	if (ret)
		return -EBUSY;

	return 0;
}

static int meson_s4_rng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct meson_rng_data *data =
			container_of(rng, struct meson_rng_data, rng);

	void __iomem *cfg_addr = data->base + RNG_S4_CFG;
	int err;

	writel_relaxed(readl_relaxed(cfg_addr) | SEED_READY_STS_BIT, cfg_addr);

	err = meson_rng_wait_status(cfg_addr, SEED_READY_STS_BIT);
	if (err) {
		dev_err(data->dev, "Seed isn't ready, try again\n");
		return err;
	}

	err = meson_rng_wait_status(cfg_addr, RUN_BIT);
	if (err) {
		dev_err(data->dev, "Can't get random number, try again\n");
		return err;
	}

	*(u32 *)buf = readl_relaxed(data->base + RNG_S4_DATA);

	return sizeof(u32);
}

static int meson_rng_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct meson_rng_data *data;
	struct clk *core_clk;
	const struct meson_rng_priv *priv;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	priv = device_get_match_data(&pdev->dev);
	if (!priv)
		return -ENODEV;

	data->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	core_clk = devm_clk_get_optional_enabled(dev, "core");
	if (IS_ERR(core_clk))
		return dev_err_probe(dev, PTR_ERR(core_clk),
				     "Failed to get core clock\n");

	data->rng.name = pdev->name;
	data->rng.read = priv->read;

	data->dev = &pdev->dev;

	return devm_hwrng_register(dev, &data->rng);
}

static const struct meson_rng_priv meson_rng_priv = {
	.read = meson_rng_read,
};

static const struct meson_rng_priv meson_rng_priv_s4 = {
	.read = meson_s4_rng_read,
};

static const struct of_device_id meson_rng_of_match[] = {
	{
		.compatible = "amlogic,meson-rng",
		.data = (void *)&meson_rng_priv,
	},
	{
		.compatible = "amlogic,meson-s4-rng",
		.data = (void *)&meson_rng_priv_s4,
	},
	{},
};
MODULE_DEVICE_TABLE(of, meson_rng_of_match);

static struct platform_driver meson_rng_driver = {
	.probe	= meson_rng_probe,
	.driver	= {
		.name = "meson-rng",
		.of_match_table = meson_rng_of_match,
	},
};

module_platform_driver(meson_rng_driver);

MODULE_DESCRIPTION("Meson H/W Random Number Generator driver");
MODULE_AUTHOR("Lawrence Mok <lawrence.mok@amlogic.com>");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_LICENSE("Dual BSD/GPL");
