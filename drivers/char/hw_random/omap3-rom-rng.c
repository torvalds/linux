/*
 * omap3-rom-rng.c - RNG driver for TI OMAP3 CPU family
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Juha Yrjola <juha.yrjola@solidboot.com>
 *
 * Copyright (C) 2013 Pali Rohár <pali@kernel.org>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/random.h>
#include <linux/hw_random.h>
#include <linux/workqueue.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#define RNG_RESET			0x01
#define RNG_GEN_PRNG_HW_INIT		0x02
#define RNG_GEN_HW			0x08

struct omap_rom_rng {
	struct clk *clk;
	struct device *dev;
	struct hwrng ops;
	u32 (*rom_rng_call)(u32 ptr, u32 count, u32 flag);
};

static int omap3_rom_rng_read(struct hwrng *rng, void *data, size_t max, bool w)
{
	struct omap_rom_rng *ddata;
	u32 ptr;
	int r;

	ddata = (struct omap_rom_rng *)rng->priv;

	r = pm_runtime_get_sync(ddata->dev);
	if (r < 0) {
		pm_runtime_put_noidle(ddata->dev);

		return r;
	}

	ptr = virt_to_phys(data);
	r = ddata->rom_rng_call(ptr, 4, RNG_GEN_HW);
	if (r != 0)
		r = -EINVAL;
	else
		r = 4;

	pm_runtime_mark_last_busy(ddata->dev);
	pm_runtime_put_autosuspend(ddata->dev);

	return r;
}

static int __maybe_unused omap_rom_rng_runtime_suspend(struct device *dev)
{
	struct omap_rom_rng *ddata;
	int r;

	ddata = dev_get_drvdata(dev);

	r = ddata->rom_rng_call(0, 0, RNG_RESET);
	if (r != 0)
		dev_err(dev, "reset failed: %d\n", r);

	clk_disable_unprepare(ddata->clk);

	return 0;
}

static int __maybe_unused omap_rom_rng_runtime_resume(struct device *dev)
{
	struct omap_rom_rng *ddata;
	int r;

	ddata = dev_get_drvdata(dev);

	r = clk_prepare_enable(ddata->clk);
	if (r < 0)
		return r;

	r = ddata->rom_rng_call(0, 0, RNG_GEN_PRNG_HW_INIT);
	if (r != 0) {
		clk_disable_unprepare(ddata->clk);
		dev_err(dev, "HW init failed: %d\n", r);

		return -EIO;
	}

	return 0;
}

static void omap_rom_rng_finish(void *data)
{
	struct omap_rom_rng *ddata = data;

	pm_runtime_dont_use_autosuspend(ddata->dev);
	pm_runtime_disable(ddata->dev);
}

static int omap3_rom_rng_probe(struct platform_device *pdev)
{
	struct omap_rom_rng *ddata;
	int ret = 0;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	ddata->dev = &pdev->dev;
	ddata->ops.priv = (unsigned long)ddata;
	ddata->ops.name = "omap3-rom";
	ddata->ops.read = of_device_get_match_data(&pdev->dev);
	ddata->ops.quality = 900;
	if (!ddata->ops.read) {
		dev_err(&pdev->dev, "missing rom code handler\n");

		return -ENODEV;
	}
	dev_set_drvdata(ddata->dev, ddata);

	ddata->rom_rng_call = pdev->dev.platform_data;
	if (!ddata->rom_rng_call) {
		dev_err(ddata->dev, "rom_rng_call is NULL\n");
		return -EINVAL;
	}

	ddata->clk = devm_clk_get(ddata->dev, "ick");
	if (IS_ERR(ddata->clk)) {
		dev_err(ddata->dev, "unable to get RNG clock\n");
		return PTR_ERR(ddata->clk);
	}

	pm_runtime_enable(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, 500);
	pm_runtime_use_autosuspend(&pdev->dev);

	ret = devm_add_action_or_reset(ddata->dev, omap_rom_rng_finish,
				       ddata);
	if (ret)
		return ret;

	return devm_hwrng_register(ddata->dev, &ddata->ops);
}

static const struct of_device_id omap_rom_rng_match[] = {
	{ .compatible = "nokia,n900-rom-rng", .data = omap3_rom_rng_read, },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, omap_rom_rng_match);

static const struct dev_pm_ops omap_rom_rng_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(omap_rom_rng_runtime_suspend,
				omap_rom_rng_runtime_resume)
};

static struct platform_driver omap3_rom_rng_driver = {
	.driver = {
		.name		= "omap3-rom-rng",
		.of_match_table = omap_rom_rng_match,
		.pm = &omap_rom_rng_pm_ops,
	},
	.probe		= omap3_rom_rng_probe,
};

module_platform_driver(omap3_rom_rng_driver);

MODULE_ALIAS("platform:omap3-rom-rng");
MODULE_AUTHOR("Juha Yrjola");
MODULE_AUTHOR("Pali Rohár <pali@kernel.org>");
MODULE_LICENSE("GPL");
