/*
 * Copyright (C) 2012 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/slab.h>

#define TSADC_DATA		0x00
#define TSADC_DATA_MASK		0xfff

#define TSADC_STAS		0x04
#define TSADC_STAS_BUSY		(1 << 0)
#define TSADC_STAS_BUSY_MASK	(1 << 0)

#define TSADC_CTRL		0x08
#define TSADC_CTRL_CH(ch)	((ch) << 0)
#define TSADC_CTRL_POWER_UP	(1 << 3)
#define TSADC_CTRL_START	(1 << 4)
#define TSADC_CTRL_IRQ_ENABLE	(1 << 5)
#define TSADC_CTRL_IRQ_STATUS	(1 << 6)

#define TSADC_DLY_PU_SOC	0x0C

#define TSADC_CLK_RATE		50000 /* 50KHz */

struct tsadc_table
{
	int code;
	int temp;
};

static const struct tsadc_table table[] =
{
	{TSADC_DATA_MASK, -40},

	{3800, -40},
	{3792, -35},
	{3783, -30},
	{3774, -25},
	{3765, -20},
	{3756, -15},
	{3747, -10},
	{3737, -5},
	{3728, 0},
	{3718, 5},

	{3708, 10},
	{3698, 15},
	{3688, 20},
	{3678, 25},
	{3667, 30},
	{3656, 35},
	{3645, 40},
	{3634, 45},
	{3623, 50},
	{3611, 55},

	{3600, 60},
	{3588, 65},
	{3575, 70},
	{3563, 75},
	{3550, 80},
	{3537, 85},
	{3524, 90},
	{3510, 95},
	{3496, 100},
	{3482, 105},

	{3467, 110},
	{3452, 115},
	{3437, 120},
	{3421, 125},

	{0, 125},
};

struct rk30_tsadc_device {
	void __iomem		*regs;
	struct clk		*clk;
	struct clk		*pclk;
	struct resource		*ioarea;
};

static struct rk30_tsadc_device *g_dev;

static u32 tsadc_readl(u32 offset)
{
	return readl_relaxed(g_dev->regs + offset);
}

static void tsadc_writel(u32 val, u32 offset)
{
	writel_relaxed(val, g_dev->regs + offset);
}

static DEFINE_MUTEX(tsadc_mutex);
static void rk30_tsadc_get(unsigned int chn, int *temp, int *code)
{
	*temp = 0;
	*code = 0;

	if (!g_dev || chn > 1)
		return;

	mutex_lock(&tsadc_mutex);

	clk_enable(g_dev->pclk);
	clk_enable(g_dev->clk);

	msleep(10);
	tsadc_writel(0, TSADC_CTRL);
	tsadc_writel(TSADC_CTRL_POWER_UP | TSADC_CTRL_CH(chn), TSADC_CTRL);
	msleep(10);
	if ((tsadc_readl(TSADC_STAS) & TSADC_STAS_BUSY_MASK) != TSADC_STAS_BUSY) {
		int i;
		*code = tsadc_readl(TSADC_DATA) & TSADC_DATA_MASK;
		for (i = 0; i < ARRAY_SIZE(table) - 1; i++) {
			if ((*code) <= table[i].code && (*code) > table[i + 1].code) {
				*temp = table[i].temp + (table[i + 1].temp - table[i].temp) * (table[i].code - (*code)) / (table[i].code - table[i + 1].code);
			}
		}
	}
	tsadc_writel(0, TSADC_CTRL);

	clk_disable(g_dev->clk);
	clk_disable(g_dev->pclk);

	mutex_unlock(&tsadc_mutex);
}

int rk30_tsadc_get_temp(unsigned int chn)
{
	int temp, code;

	rk30_tsadc_get(chn, &temp, &code);
	return temp;
}
EXPORT_SYMBOL(rk30_tsadc_get_temp);

static int rk30_tsadc_get_temp0(char *buffer, struct kernel_param *kp)
{
	int temp, code;
	rk30_tsadc_get(0, &temp, &code);
	return sprintf(buffer, "temp: %d code: %d", temp, code);
}
module_param_call(temp0, NULL, rk30_tsadc_get_temp0, NULL, S_IRUGO);

static int rk30_tsadc_get_temp1(char *buffer, struct kernel_param *kp)
{
	int temp, code;
	rk30_tsadc_get(1, &temp, &code);
	return sprintf(buffer, "temp: %d code: %d", temp, code);
}
module_param_call(temp1, NULL, rk30_tsadc_get_temp1, NULL, S_IRUGO);

static int __init rk30_tsadc_probe(struct platform_device *pdev)
{
	struct rk30_tsadc_device *dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	struct resource *res;
	int ret;

	if (!dev) {
		dev_err(&pdev->dev, "failed to alloc mem\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "cannot find IO resource\n");
		ret = -ENOENT;
		goto err1;
	}

	dev->ioarea = request_mem_region(res->start, (res->end - res->start) + 1, pdev->name);
	if (!dev->ioarea) {
		dev_err(&pdev->dev, "cannot request IO\n");
		ret = -ENXIO;
		goto err1;
	}

	dev->regs = ioremap(res->start, (res->end - res->start) + 1);
	if (!dev->regs) {
		dev_err(&pdev->dev, "cannot map IO\n");
		ret = -ENXIO;
		goto err2;
	}

	dev->clk = clk_get(NULL, "tsadc");
	if (IS_ERR(dev->clk)) {
		dev_err(&pdev->dev, "failed to get clk\n");
		ret = PTR_ERR(dev->clk);
		goto err3;
	}

	ret = clk_set_rate(dev->clk, TSADC_CLK_RATE);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to set clk\n");
		goto err4;
	}

	dev->pclk = clk_get(NULL, "pclk_tsadc");
	if (IS_ERR(dev->pclk)) {
		dev_err(&pdev->dev, "failed to get pclk\n");
		ret = PTR_ERR(dev->clk);
		goto err4;
	}

	platform_set_drvdata(pdev, dev);
	g_dev = dev;

	dev_info(&pdev->dev, "initialized\n");

	return 0;

err4:
	clk_put(dev->clk);
err3:
	iounmap(dev->regs);
err2:
	release_resource(dev->ioarea);
err1:
	kfree(dev);
	return ret;
}

static struct platform_driver rk30_tsadc_driver = {
	.driver		= {
		.name	= "rk30-tsadc",
		.owner	= THIS_MODULE,
	},
};

static int __init rk30_tsadc_init(void)
{
	return platform_driver_probe(&rk30_tsadc_driver, rk30_tsadc_probe);
}
rootfs_initcall(rk30_tsadc_init);

MODULE_DESCRIPTION("Driver for TSADC");
MODULE_AUTHOR("lw, lw@rock-chips.com");
MODULE_LICENSE("GPL");
