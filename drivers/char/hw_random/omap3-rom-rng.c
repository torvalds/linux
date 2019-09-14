/*
 * omap3-rom-rng.c - RNG driver for TI OMAP3 CPU family
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Juha Yrjola <juha.yrjola@solidboot.com>
 *
 * Copyright (C) 2013 Pali Rohár <pali.rohar@gmail.com>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/random.h>
#include <linux/hw_random.h>
#include <linux/workqueue.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>

#define RNG_RESET			0x01
#define RNG_GEN_PRNG_HW_INIT		0x02
#define RNG_GEN_HW			0x08

/* param1: ptr, param2: count, param3: flag */
static u32 (*omap3_rom_rng_call)(u32, u32, u32);

static struct delayed_work idle_work;
static int rng_idle;
static struct clk *rng_clk;

static void omap3_rom_rng_idle(struct work_struct *work)
{
	int r;

	r = omap3_rom_rng_call(0, 0, RNG_RESET);
	if (r != 0) {
		pr_err("reset failed: %d\n", r);
		return;
	}
	clk_disable_unprepare(rng_clk);
	rng_idle = 1;
}

static int omap3_rom_rng_get_random(void *buf, unsigned int count)
{
	u32 r;
	u32 ptr;

	cancel_delayed_work_sync(&idle_work);
	if (rng_idle) {
		r = clk_prepare_enable(rng_clk);
		if (r)
			return r;

		r = omap3_rom_rng_call(0, 0, RNG_GEN_PRNG_HW_INIT);
		if (r != 0) {
			clk_disable_unprepare(rng_clk);
			pr_err("HW init failed: %d\n", r);
			return -EIO;
		}
		rng_idle = 0;
	}

	ptr = virt_to_phys(buf);
	r = omap3_rom_rng_call(ptr, count, RNG_GEN_HW);
	schedule_delayed_work(&idle_work, msecs_to_jiffies(500));
	if (r != 0)
		return -EINVAL;
	return 0;
}

static int omap3_rom_rng_read(struct hwrng *rng, void *data, size_t max, bool w)
{
	int r;

	r = omap3_rom_rng_get_random(data, 4);
	if (r < 0)
		return r;
	return 4;
}

static struct hwrng omap3_rom_rng_ops = {
	.name		= "omap3-rom",
	.read		= omap3_rom_rng_read,
};

static int omap3_rom_rng_probe(struct platform_device *pdev)
{
	int ret = 0;

	pr_info("initializing\n");

	omap3_rom_rng_call = pdev->dev.platform_data;
	if (!omap3_rom_rng_call) {
		pr_err("omap3_rom_rng_call is NULL\n");
		return -EINVAL;
	}

	INIT_DELAYED_WORK(&idle_work, omap3_rom_rng_idle);
	rng_clk = devm_clk_get(&pdev->dev, "ick");
	if (IS_ERR(rng_clk)) {
		pr_err("unable to get RNG clock\n");
		return PTR_ERR(rng_clk);
	}

	/* Leave the RNG in reset state. */
	ret = clk_prepare_enable(rng_clk);
	if (ret)
		return ret;
	omap3_rom_rng_idle(0);

	return hwrng_register(&omap3_rom_rng_ops);
}

static int omap3_rom_rng_remove(struct platform_device *pdev)
{
	cancel_delayed_work_sync(&idle_work);
	hwrng_unregister(&omap3_rom_rng_ops);
	if (!rng_idle)
		clk_disable_unprepare(rng_clk);
	return 0;
}

static struct platform_driver omap3_rom_rng_driver = {
	.driver = {
		.name		= "omap3-rom-rng",
	},
	.probe		= omap3_rom_rng_probe,
	.remove		= omap3_rom_rng_remove,
};

module_platform_driver(omap3_rom_rng_driver);

MODULE_ALIAS("platform:omap3-rom-rng");
MODULE_AUTHOR("Juha Yrjola");
MODULE_AUTHOR("Pali Rohár <pali.rohar@gmail.com>");
MODULE_LICENSE("GPL");
