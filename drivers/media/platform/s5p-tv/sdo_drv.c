/*
 * Samsung Standard Definition Output (SDO) driver
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *
 * Tomasz Stanislawski, <t.stanislaws@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundiation. either version 2 of the License,
 * or (at your option) any later version
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <media/v4l2-subdev.h>

#include "regs-sdo.h"

MODULE_AUTHOR("Tomasz Stanislawski, <t.stanislaws@samsung.com>");
MODULE_DESCRIPTION("Samsung Standard Definition Output (SDO)");
MODULE_LICENSE("GPL");

#define SDO_DEFAULT_STD	V4L2_STD_PAL

struct sdo_format {
	v4l2_std_id id;
	/* all modes are 720 pixels wide */
	unsigned int height;
	unsigned int cookie;
};

struct sdo_device {
	/** pointer to device parent */
	struct device *dev;
	/** base address of SDO registers */
	void __iomem *regs;
	/** SDO interrupt */
	unsigned int irq;
	/** DAC source clock */
	struct clk *sclk_dac;
	/** DAC clock */
	struct clk *dac;
	/** DAC physical interface */
	struct clk *dacphy;
	/** clock for control of VPLL */
	struct clk *fout_vpll;
	/** regulator for SDO IP power */
	struct regulator *vdac;
	/** regulator for SDO plug detection */
	struct regulator *vdet;
	/** subdev used as device interface */
	struct v4l2_subdev sd;
	/** current format */
	const struct sdo_format *fmt;
};

static inline struct sdo_device *sd_to_sdev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct sdo_device, sd);
}

static inline
void sdo_write_mask(struct sdo_device *sdev, u32 reg_id, u32 value, u32 mask)
{
	u32 old = readl(sdev->regs + reg_id);
	value = (value & mask) | (old & ~mask);
	writel(value, sdev->regs + reg_id);
}

static inline
void sdo_write(struct sdo_device *sdev, u32 reg_id, u32 value)
{
	writel(value, sdev->regs + reg_id);
}

static inline
u32 sdo_read(struct sdo_device *sdev, u32 reg_id)
{
	return readl(sdev->regs + reg_id);
}

static irqreturn_t sdo_irq_handler(int irq, void *dev_data)
{
	struct sdo_device *sdev = dev_data;

	/* clear interrupt */
	sdo_write_mask(sdev, SDO_IRQ, ~0, SDO_VSYNC_IRQ_PEND);
	return IRQ_HANDLED;
}

static void sdo_reg_debug(struct sdo_device *sdev)
{
#define DBGREG(reg_id) \
	dev_info(sdev->dev, #reg_id " = %08x\n", \
		sdo_read(sdev, reg_id))

	DBGREG(SDO_CLKCON);
	DBGREG(SDO_CONFIG);
	DBGREG(SDO_VBI);
	DBGREG(SDO_DAC);
	DBGREG(SDO_IRQ);
	DBGREG(SDO_IRQMASK);
	DBGREG(SDO_VERSION);
}

static const struct sdo_format sdo_format[] = {
	{ V4L2_STD_PAL_N,	.height = 576, .cookie = SDO_PAL_N },
	{ V4L2_STD_PAL_Nc,	.height = 576, .cookie = SDO_PAL_NC },
	{ V4L2_STD_PAL_M,	.height = 480, .cookie = SDO_PAL_M },
	{ V4L2_STD_PAL_60,	.height = 480, .cookie = SDO_PAL_60 },
	{ V4L2_STD_NTSC_443,	.height = 480, .cookie = SDO_NTSC_443 },
	{ V4L2_STD_PAL,		.height = 576, .cookie = SDO_PAL_BGHID },
	{ V4L2_STD_NTSC_M,	.height = 480, .cookie = SDO_NTSC_M },
};

static const struct sdo_format *sdo_find_format(v4l2_std_id id)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(sdo_format); ++i)
		if (sdo_format[i].id & id)
			return &sdo_format[i];
	return NULL;
}

static int sdo_g_tvnorms_output(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	*std = V4L2_STD_NTSC_M | V4L2_STD_PAL_M | V4L2_STD_PAL |
		V4L2_STD_PAL_N | V4L2_STD_PAL_Nc |
		V4L2_STD_NTSC_443 | V4L2_STD_PAL_60;
	return 0;
}

static int sdo_s_std_output(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct sdo_device *sdev = sd_to_sdev(sd);
	const struct sdo_format *fmt;
	fmt = sdo_find_format(std);
	if (fmt == NULL)
		return -EINVAL;
	sdev->fmt = fmt;
	return 0;
}

static int sdo_g_std_output(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	*std = sd_to_sdev(sd)->fmt->id;
	return 0;
}

static int sdo_g_mbus_fmt(struct v4l2_subdev *sd,
	struct v4l2_mbus_framefmt *fmt)
{
	struct sdo_device *sdev = sd_to_sdev(sd);

	if (!sdev->fmt)
		return -ENXIO;
	/* all modes are 720 pixels wide */
	fmt->width = 720;
	fmt->height = sdev->fmt->height;
	fmt->code = V4L2_MBUS_FMT_FIXED;
	fmt->field = V4L2_FIELD_INTERLACED;
	fmt->colorspace = V4L2_COLORSPACE_JPEG;
	return 0;
}

static int sdo_s_power(struct v4l2_subdev *sd, int on)
{
	struct sdo_device *sdev = sd_to_sdev(sd);
	struct device *dev = sdev->dev;
	int ret;

	dev_info(dev, "sdo_s_power(%d)\n", on);

	if (on)
		ret = pm_runtime_get_sync(dev);
	else
		ret = pm_runtime_put_sync(dev);

	/* only values < 0 indicate errors */
	return IS_ERR_VALUE(ret) ? ret : 0;
}

static int sdo_streamon(struct sdo_device *sdev)
{
	/* set proper clock for Timing Generator */
	clk_set_rate(sdev->fout_vpll, 54000000);
	dev_info(sdev->dev, "fout_vpll.rate = %lu\n",
	clk_get_rate(sdev->fout_vpll));
	/* enable clock in SDO */
	sdo_write_mask(sdev, SDO_CLKCON, ~0, SDO_TVOUT_CLOCK_ON);
	clk_enable(sdev->dacphy);
	/* enable DAC */
	sdo_write_mask(sdev, SDO_DAC, ~0, SDO_POWER_ON_DAC);
	sdo_reg_debug(sdev);
	return 0;
}

static int sdo_streamoff(struct sdo_device *sdev)
{
	int tries;

	sdo_write_mask(sdev, SDO_DAC, 0, SDO_POWER_ON_DAC);
	clk_disable(sdev->dacphy);
	sdo_write_mask(sdev, SDO_CLKCON, 0, SDO_TVOUT_CLOCK_ON);
	for (tries = 100; tries; --tries) {
		if (sdo_read(sdev, SDO_CLKCON) & SDO_TVOUT_CLOCK_READY)
			break;
		mdelay(1);
	}
	if (tries == 0)
		dev_err(sdev->dev, "failed to stop streaming\n");
	return tries ? 0 : -EIO;
}

static int sdo_s_stream(struct v4l2_subdev *sd, int on)
{
	struct sdo_device *sdev = sd_to_sdev(sd);
	return on ? sdo_streamon(sdev) : sdo_streamoff(sdev);
}

static const struct v4l2_subdev_core_ops sdo_sd_core_ops = {
	.s_power = sdo_s_power,
};

static const struct v4l2_subdev_video_ops sdo_sd_video_ops = {
	.s_std_output = sdo_s_std_output,
	.g_std_output = sdo_g_std_output,
	.g_tvnorms_output = sdo_g_tvnorms_output,
	.g_mbus_fmt = sdo_g_mbus_fmt,
	.s_stream = sdo_s_stream,
};

static const struct v4l2_subdev_ops sdo_sd_ops = {
	.core = &sdo_sd_core_ops,
	.video = &sdo_sd_video_ops,
};

static int sdo_runtime_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct sdo_device *sdev = sd_to_sdev(sd);

	dev_info(dev, "suspend\n");
	regulator_disable(sdev->vdet);
	regulator_disable(sdev->vdac);
	clk_disable(sdev->sclk_dac);
	return 0;
}

static int sdo_runtime_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct sdo_device *sdev = sd_to_sdev(sd);

	dev_info(dev, "resume\n");
	clk_enable(sdev->sclk_dac);
	regulator_enable(sdev->vdac);
	regulator_enable(sdev->vdet);

	/* software reset */
	sdo_write_mask(sdev, SDO_CLKCON, ~0, SDO_TVOUT_SW_RESET);
	mdelay(10);
	sdo_write_mask(sdev, SDO_CLKCON, 0, SDO_TVOUT_SW_RESET);

	/* setting TV mode */
	sdo_write_mask(sdev, SDO_CONFIG, sdev->fmt->cookie, SDO_STANDARD_MASK);
	/* XXX: forcing interlaced mode using undocumented bit */
	sdo_write_mask(sdev, SDO_CONFIG, 0, SDO_PROGRESSIVE);
	/* turn all VBI off */
	sdo_write_mask(sdev, SDO_VBI, 0, SDO_CVBS_WSS_INS |
		SDO_CVBS_CLOSED_CAPTION_MASK);
	/* turn all post processing off */
	sdo_write_mask(sdev, SDO_CCCON, ~0, SDO_COMPENSATION_BHS_ADJ_OFF |
		SDO_COMPENSATION_CVBS_COMP_OFF);
	sdo_reg_debug(sdev);
	return 0;
}

static const struct dev_pm_ops sdo_pm_ops = {
	.runtime_suspend = sdo_runtime_suspend,
	.runtime_resume	 = sdo_runtime_resume,
};

static int sdo_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sdo_device *sdev;
	struct resource *res;
	int ret = 0;
	struct clk *sclk_vpll;

	dev_info(dev, "probe start\n");
	sdev = devm_kzalloc(&pdev->dev, sizeof *sdev, GFP_KERNEL);
	if (!sdev) {
		dev_err(dev, "not enough memory.\n");
		ret = -ENOMEM;
		goto fail;
	}
	sdev->dev = dev;

	/* mapping registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(dev, "get memory resource failed.\n");
		ret = -ENXIO;
		goto fail;
	}

	sdev->regs = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (sdev->regs == NULL) {
		dev_err(dev, "register mapping failed.\n");
		ret = -ENXIO;
		goto fail;
	}

	/* acquiring interrupt */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (res == NULL) {
		dev_err(dev, "get interrupt resource failed.\n");
		ret = -ENXIO;
		goto fail;
	}
	ret = devm_request_irq(&pdev->dev, res->start, sdo_irq_handler, 0,
			       "s5p-sdo", sdev);
	if (ret) {
		dev_err(dev, "request interrupt failed.\n");
		goto fail;
	}
	sdev->irq = res->start;

	/* acquire clocks */
	sdev->sclk_dac = clk_get(dev, "sclk_dac");
	if (IS_ERR_OR_NULL(sdev->sclk_dac)) {
		dev_err(dev, "failed to get clock 'sclk_dac'\n");
		ret = -ENXIO;
		goto fail;
	}
	sdev->dac = clk_get(dev, "dac");
	if (IS_ERR_OR_NULL(sdev->dac)) {
		dev_err(dev, "failed to get clock 'dac'\n");
		ret = -ENXIO;
		goto fail_sclk_dac;
	}
	sdev->dacphy = clk_get(dev, "dacphy");
	if (IS_ERR_OR_NULL(sdev->dacphy)) {
		dev_err(dev, "failed to get clock 'dacphy'\n");
		ret = -ENXIO;
		goto fail_dac;
	}
	sclk_vpll = clk_get(dev, "sclk_vpll");
	if (IS_ERR_OR_NULL(sclk_vpll)) {
		dev_err(dev, "failed to get clock 'sclk_vpll'\n");
		ret = -ENXIO;
		goto fail_dacphy;
	}
	clk_set_parent(sdev->sclk_dac, sclk_vpll);
	clk_put(sclk_vpll);
	sdev->fout_vpll = clk_get(dev, "fout_vpll");
	if (IS_ERR_OR_NULL(sdev->fout_vpll)) {
		dev_err(dev, "failed to get clock 'fout_vpll'\n");
		goto fail_dacphy;
	}
	dev_info(dev, "fout_vpll.rate = %lu\n", clk_get_rate(sclk_vpll));

	/* acquire regulator */
	sdev->vdac = devm_regulator_get(dev, "vdd33a_dac");
	if (IS_ERR_OR_NULL(sdev->vdac)) {
		dev_err(dev, "failed to get regulator 'vdac'\n");
		goto fail_fout_vpll;
	}
	sdev->vdet = devm_regulator_get(dev, "vdet");
	if (IS_ERR_OR_NULL(sdev->vdet)) {
		dev_err(dev, "failed to get regulator 'vdet'\n");
		goto fail_fout_vpll;
	}

	/* enable gate for dac clock, because mixer uses it */
	clk_enable(sdev->dac);

	/* configure power management */
	pm_runtime_enable(dev);

	/* configuration of interface subdevice */
	v4l2_subdev_init(&sdev->sd, &sdo_sd_ops);
	sdev->sd.owner = THIS_MODULE;
	strlcpy(sdev->sd.name, "s5p-sdo", sizeof sdev->sd.name);

	/* set default format */
	sdev->fmt = sdo_find_format(SDO_DEFAULT_STD);
	BUG_ON(sdev->fmt == NULL);

	/* keeping subdev in device's private for use by other drivers */
	dev_set_drvdata(dev, &sdev->sd);

	dev_info(dev, "probe succeeded\n");
	return 0;

fail_fout_vpll:
	clk_put(sdev->fout_vpll);
fail_dacphy:
	clk_put(sdev->dacphy);
fail_dac:
	clk_put(sdev->dac);
fail_sclk_dac:
	clk_put(sdev->sclk_dac);
fail:
	dev_info(dev, "probe failed\n");
	return ret;
}

static int sdo_remove(struct platform_device *pdev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(&pdev->dev);
	struct sdo_device *sdev = sd_to_sdev(sd);

	pm_runtime_disable(&pdev->dev);
	clk_disable(sdev->dac);
	clk_put(sdev->fout_vpll);
	clk_put(sdev->dacphy);
	clk_put(sdev->dac);
	clk_put(sdev->sclk_dac);

	dev_info(&pdev->dev, "remove successful\n");
	return 0;
}

static struct platform_driver sdo_driver __refdata = {
	.probe = sdo_probe,
	.remove = sdo_remove,
	.driver = {
		.name = "s5p-sdo",
		.owner = THIS_MODULE,
		.pm = &sdo_pm_ops,
	}
};

module_platform_driver(sdo_driver);
