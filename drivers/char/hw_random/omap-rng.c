/*
 * omap-rng.c - RNG driver for TI OMAP CPU family
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright 2005 (c) MontaVista Software, Inc.
 *
 * Mostly based on original driver:
 *
 * Copyright (C) 2005 Nokia Corporation
 * Author: Juha Yrjölä <juha.yrjola@nokia.com>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/random.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/hw_random.h>
#include <linux/delay.h>

#include <asm/io.h>

#define RNG_OUT_REG		0x00		/* Output register */
#define RNG_STAT_REG		0x04		/* Status register
							[0] = STAT_BUSY */
#define RNG_ALARM_REG		0x24		/* Alarm register
							[7:0] = ALARM_COUNTER */
#define RNG_CONFIG_REG		0x28		/* Configuration register
							[11:6] = RESET_COUNT
							[5:3]  = RING2_DELAY
							[2:0]  = RING1_DELAY */
#define RNG_REV_REG		0x3c		/* Revision register
							[7:0] = REV_NB */
#define RNG_MASK_REG		0x40		/* Mask and reset register
							[2] = IT_EN
							[1] = SOFTRESET
							[0] = AUTOIDLE */
#define RNG_SYSSTATUS		0x44		/* System status
							[0] = RESETDONE */

static void __iomem *rng_base;
static struct clk *rng_ick;
static struct platform_device *rng_dev;

static inline u32 omap_rng_read_reg(int reg)
{
	return __raw_readl(rng_base + reg);
}

static inline void omap_rng_write_reg(int reg, u32 val)
{
	__raw_writel(val, rng_base + reg);
}

static int omap_rng_data_present(struct hwrng *rng, int wait)
{
	int data, i;

	for (i = 0; i < 20; i++) {
		data = omap_rng_read_reg(RNG_STAT_REG) ? 0 : 1;
		if (data || !wait)
			break;
		/* RNG produces data fast enough (2+ MBit/sec, even
		 * during "rngtest" loads, that these delays don't
		 * seem to trigger.  We *could* use the RNG IRQ, but
		 * that'd be higher overhead ... so why bother?
		 */
		udelay(10);
	}
	return data;
}

static int omap_rng_data_read(struct hwrng *rng, u32 *data)
{
	*data = omap_rng_read_reg(RNG_OUT_REG);

	return 4;
}

static struct hwrng omap_rng_ops = {
	.name		= "omap",
	.data_present	= omap_rng_data_present,
	.data_read	= omap_rng_data_read,
};

static int __init omap_rng_probe(struct platform_device *pdev)
{
	struct resource *res, *mem;
	int ret;

	/*
	 * A bit ugly, and it will never actually happen but there can
	 * be only one RNG and this catches any bork
	 */
	if (rng_dev)
		return -EBUSY;

	if (cpu_is_omap24xx()) {
		rng_ick = clk_get(NULL, "rng_ick");
		if (IS_ERR(rng_ick)) {
			dev_err(&pdev->dev, "Could not get rng_ick\n");
			ret = PTR_ERR(rng_ick);
			return ret;
		} else
			clk_enable(rng_ick);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!res)
		return -ENOENT;

	mem = request_mem_region(res->start, res->end - res->start + 1,
				 pdev->name);
	if (mem == NULL)
		return -EBUSY;

	dev_set_drvdata(&pdev->dev, mem);
	rng_base = (u32 __force __iomem *)io_p2v(res->start);

	ret = hwrng_register(&omap_rng_ops);
	if (ret) {
		release_resource(mem);
		rng_base = NULL;
		return ret;
	}

	dev_info(&pdev->dev, "OMAP Random Number Generator ver. %02x\n",
		omap_rng_read_reg(RNG_REV_REG));
	omap_rng_write_reg(RNG_MASK_REG, 0x1);

	rng_dev = pdev;

	return 0;
}

static int __exit omap_rng_remove(struct platform_device *pdev)
{
	struct resource *mem = dev_get_drvdata(&pdev->dev);

	hwrng_unregister(&omap_rng_ops);

	omap_rng_write_reg(RNG_MASK_REG, 0x0);

	if (cpu_is_omap24xx()) {
		clk_disable(rng_ick);
		clk_put(rng_ick);
	}

	release_resource(mem);
	rng_base = NULL;

	return 0;
}

#ifdef CONFIG_PM

static int omap_rng_suspend(struct platform_device *pdev, pm_message_t message)
{
	omap_rng_write_reg(RNG_MASK_REG, 0x0);
	return 0;
}

static int omap_rng_resume(struct platform_device *pdev)
{
	omap_rng_write_reg(RNG_MASK_REG, 0x1);
	return 0;
}

#else

#define	omap_rng_suspend	NULL
#define	omap_rng_resume		NULL

#endif

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:omap_rng");

static struct platform_driver omap_rng_driver = {
	.driver = {
		.name		= "omap_rng",
		.owner		= THIS_MODULE,
	},
	.probe		= omap_rng_probe,
	.remove		= __exit_p(omap_rng_remove),
	.suspend	= omap_rng_suspend,
	.resume		= omap_rng_resume
};

static int __init omap_rng_init(void)
{
	if (!cpu_is_omap16xx() && !cpu_is_omap24xx())
		return -ENODEV;

	return platform_driver_register(&omap_rng_driver);
}

static void __exit omap_rng_exit(void)
{
	platform_driver_unregister(&omap_rng_driver);
}

module_init(omap_rng_init);
module_exit(omap_rng_exit);

MODULE_AUTHOR("Deepak Saxena (and others)");
MODULE_LICENSE("GPL");
