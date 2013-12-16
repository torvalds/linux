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
#include <linux/timer.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>

#define RNG_RESET			0x01
#define RNG_GEN_PRNG_HW_INIT		0x02
#define RNG_GEN_HW			0x08

/* param1: ptr, param2: count, param3: flag */
static u32 (*omap3_rom_rng_call)(u32, u32, u32);

static struct timer_list idle_timer;
static int rng_idle;
static struct clk *rng_clk;

static void omap3_rom_rng_idle(unsigned long data)
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

	del_timer_sync(&idle_timer);
	if (rng_idle) {
		clk_prepare_enable(rng_clk);
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
	mod_timer(&idle_timer, jiffies + msecs_to_jiffies(500));
	if (r != 0)
		return -EINVAL;
	return 0;
}

static int omap3_rom_rng_data_present(struct hwrng *rng, int wait)
{
	return 1;
}

static int omap3_rom_rng_data_read(struct hwrng *rng, u32 *data)
{
	int r;

	r = omap3_rom_rng_get_random(data, 4);
	if (r < 0)
		return r;
	return 4;
}

static struct hwrng omap3_rom_rng_ops = {
	.name		= "omap3-rom",
	.data_present	= omap3_rom_rng_data_present,
	.data_read	= omap3_rom_rng_data_read,
};

static int omap3_rom_rng_probe(struct platform_device *pdev)
{
	pr_info("initializing\n");

	omap3_rom_rng_call = pdev->dev.platform_data;
	if (!omap3_rom_rng_call) {
		pr_err("omap3_rom_rng_call is NULL\n");
		return -EINVAL;
	}

	setup_timer(&idle_timer, omap3_rom_rng_idle, 0);
	rng_clk = clk_get(&pdev->dev, "ick");
	if (IS_ERR(rng_clk)) {
		pr_err("unable to get RNG clock\n");
		return PTR_ERR(rng_clk);
	}

	/* Leave the RNG in reset state. */
	clk_prepare_enable(rng_clk);
	omap3_rom_rng_idle(0);

	return hwrng_register(&omap3_rom_rng_ops);
}

static int omap3_rom_rng_remove(struct platform_device *pdev)
{
	hwrng_unregister(&omap3_rom_rng_ops);
	clk_disable_unprepare(rng_clk);
	clk_put(rng_clk);
	return 0;
}

static struct platform_driver omap3_rom_rng_driver = {
	.driver = {
		.name		= "omap3-rom-rng",
		.owner		= THIS_MODULE,
	},
	.probe		= omap3_rom_rng_probe,
	.remove		= omap3_rom_rng_remove,
};

module_platform_driver(omap3_rom_rng_driver);

MODULE_ALIAS("platform:omap3-rom-rng");
MODULE_AUTHOR("Juha Yrjola");
MODULE_AUTHOR("Pali Rohár <pali.rohar@gmail.com>");
MODULE_LICENSE("GPL");
