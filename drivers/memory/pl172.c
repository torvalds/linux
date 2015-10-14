/*
 * Memory controller driver for ARM PrimeCell PL172
 * PrimeCell MultiPort Memory Controller (PL172)
 *
 * Copyright (C) 2015 Joachim Eastwood <manabian@gmail.com>
 *
 * Based on:
 * TI AEMIF driver, Copyright (C) 2010 - 2013 Texas Instruments Inc.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/amba/bus.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/time.h>

#define MPMC_STATIC_CFG(n)		(0x200 + 0x20 * n)
#define  MPMC_STATIC_CFG_MW_8BIT	0x0
#define  MPMC_STATIC_CFG_MW_16BIT	0x1
#define  MPMC_STATIC_CFG_MW_32BIT	0x2
#define  MPMC_STATIC_CFG_PM		BIT(3)
#define  MPMC_STATIC_CFG_PC		BIT(6)
#define  MPMC_STATIC_CFG_PB		BIT(7)
#define  MPMC_STATIC_CFG_EW		BIT(8)
#define  MPMC_STATIC_CFG_B		BIT(19)
#define  MPMC_STATIC_CFG_P		BIT(20)
#define MPMC_STATIC_WAIT_WEN(n)		(0x204 + 0x20 * n)
#define  MPMC_STATIC_WAIT_WEN_MAX	0x0f
#define MPMC_STATIC_WAIT_OEN(n)		(0x208 + 0x20 * n)
#define  MPMC_STATIC_WAIT_OEN_MAX	0x0f
#define MPMC_STATIC_WAIT_RD(n)		(0x20c + 0x20 * n)
#define  MPMC_STATIC_WAIT_RD_MAX	0x1f
#define MPMC_STATIC_WAIT_PAGE(n)	(0x210 + 0x20 * n)
#define  MPMC_STATIC_WAIT_PAGE_MAX	0x1f
#define MPMC_STATIC_WAIT_WR(n)		(0x214 + 0x20 * n)
#define  MPMC_STATIC_WAIT_WR_MAX	0x1f
#define MPMC_STATIC_WAIT_TURN(n)	(0x218 + 0x20 * n)
#define  MPMC_STATIC_WAIT_TURN_MAX	0x0f

/* Maximum number of static chip selects */
#define PL172_MAX_CS		4

struct pl172_data {
	void __iomem *base;
	unsigned long rate;
	struct clk *clk;
};

static int pl172_timing_prop(struct amba_device *adev,
			     const struct device_node *np, const char *name,
			     u32 reg_offset, u32 max, int start)
{
	struct pl172_data *pl172 = amba_get_drvdata(adev);
	int cycles;
	u32 val;

	if (!of_property_read_u32(np, name, &val)) {
		cycles = DIV_ROUND_UP(val * pl172->rate, NSEC_PER_MSEC) - start;
		if (cycles < 0) {
			cycles = 0;
		} else if (cycles > max) {
			dev_err(&adev->dev, "%s timing too tight\n", name);
			return -EINVAL;
		}

		writel(cycles, pl172->base + reg_offset);
	}

	dev_dbg(&adev->dev, "%s: %u cycle(s)\n", name, start +
				readl(pl172->base + reg_offset));

	return 0;
}

static int pl172_setup_static(struct amba_device *adev,
			      struct device_node *np, u32 cs)
{
	struct pl172_data *pl172 = amba_get_drvdata(adev);
	u32 cfg;
	int ret;

	/* MPMC static memory configuration */
	if (!of_property_read_u32(np, "mpmc,memory-width", &cfg)) {
		if (cfg == 8) {
			cfg = MPMC_STATIC_CFG_MW_8BIT;
		} else if (cfg == 16) {
			cfg = MPMC_STATIC_CFG_MW_16BIT;
		} else if (cfg == 32) {
			cfg = MPMC_STATIC_CFG_MW_32BIT;
		} else {
			dev_err(&adev->dev, "invalid memory width cs%u\n", cs);
			return -EINVAL;
		}
	} else {
		dev_err(&adev->dev, "memory-width property required\n");
		return -EINVAL;
	}

	if (of_property_read_bool(np, "mpmc,async-page-mode"))
		cfg |= MPMC_STATIC_CFG_PM;

	if (of_property_read_bool(np, "mpmc,cs-active-high"))
		cfg |= MPMC_STATIC_CFG_PC;

	if (of_property_read_bool(np, "mpmc,byte-lane-low"))
		cfg |= MPMC_STATIC_CFG_PB;

	if (of_property_read_bool(np, "mpmc,extended-wait"))
		cfg |= MPMC_STATIC_CFG_EW;

	if (of_property_read_bool(np, "mpmc,buffer-enable"))
		cfg |= MPMC_STATIC_CFG_B;

	if (of_property_read_bool(np, "mpmc,write-protect"))
		cfg |= MPMC_STATIC_CFG_P;

	writel(cfg, pl172->base + MPMC_STATIC_CFG(cs));
	dev_dbg(&adev->dev, "mpmc static config cs%u: 0x%08x\n", cs, cfg);

	/* MPMC static memory timing */
	ret = pl172_timing_prop(adev, np, "mpmc,write-enable-delay",
				MPMC_STATIC_WAIT_WEN(cs),
				MPMC_STATIC_WAIT_WEN_MAX, 1);
	if (ret)
		goto fail;

	ret = pl172_timing_prop(adev, np, "mpmc,output-enable-delay",
				MPMC_STATIC_WAIT_OEN(cs),
				MPMC_STATIC_WAIT_OEN_MAX, 0);
	if (ret)
		goto fail;

	ret = pl172_timing_prop(adev, np, "mpmc,read-access-delay",
				MPMC_STATIC_WAIT_RD(cs),
				MPMC_STATIC_WAIT_RD_MAX, 1);
	if (ret)
		goto fail;

	ret = pl172_timing_prop(adev, np, "mpmc,page-mode-read-delay",
				MPMC_STATIC_WAIT_PAGE(cs),
				MPMC_STATIC_WAIT_PAGE_MAX, 1);
	if (ret)
		goto fail;

	ret = pl172_timing_prop(adev, np, "mpmc,write-access-delay",
				MPMC_STATIC_WAIT_WR(cs),
				MPMC_STATIC_WAIT_WR_MAX, 2);
	if (ret)
		goto fail;

	ret = pl172_timing_prop(adev, np, "mpmc,turn-round-delay",
				MPMC_STATIC_WAIT_TURN(cs),
				MPMC_STATIC_WAIT_TURN_MAX, 1);
	if (ret)
		goto fail;

	return 0;
fail:
	dev_err(&adev->dev, "failed to configure cs%u\n", cs);
	return ret;
}

static int pl172_parse_cs_config(struct amba_device *adev,
				 struct device_node *np)
{
	u32 cs;

	if (!of_property_read_u32(np, "mpmc,cs", &cs)) {
		if (cs >= PL172_MAX_CS) {
			dev_err(&adev->dev, "cs%u invalid\n", cs);
			return -EINVAL;
		}

		return pl172_setup_static(adev, np, cs);
	}

	dev_err(&adev->dev, "cs property required\n");

	return -EINVAL;
}

static const char * const pl172_revisions[] = {"r1", "r2", "r2p3", "r2p4"};

static int pl172_probe(struct amba_device *adev, const struct amba_id *id)
{
	struct device_node *child_np, *np = adev->dev.of_node;
	struct device *dev = &adev->dev;
	static const char *rev = "?";
	struct pl172_data *pl172;
	int ret;

	if (amba_part(adev) == 0x172) {
		if (amba_rev(adev) < ARRAY_SIZE(pl172_revisions))
			rev = pl172_revisions[amba_rev(adev)];
	}

	dev_info(dev, "ARM PL%x revision %s\n", amba_part(adev), rev);

	pl172 = devm_kzalloc(dev, sizeof(*pl172), GFP_KERNEL);
	if (!pl172)
		return -ENOMEM;

	pl172->clk = devm_clk_get(dev, "mpmcclk");
	if (IS_ERR(pl172->clk)) {
		dev_err(dev, "no mpmcclk provided clock\n");
		return PTR_ERR(pl172->clk);
	}

	ret = clk_prepare_enable(pl172->clk);
	if (ret) {
		dev_err(dev, "unable to mpmcclk enable clock\n");
		return ret;
	}

	pl172->rate = clk_get_rate(pl172->clk) / MSEC_PER_SEC;
	if (!pl172->rate) {
		dev_err(dev, "unable to get mpmcclk clock rate\n");
		ret = -EINVAL;
		goto err_clk_enable;
	}

	ret = amba_request_regions(adev, NULL);
	if (ret) {
		dev_err(dev, "unable to request AMBA regions\n");
		goto err_clk_enable;
	}

	pl172->base = devm_ioremap(dev, adev->res.start,
				   resource_size(&adev->res));
	if (!pl172->base) {
		dev_err(dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto err_no_ioremap;
	}

	amba_set_drvdata(adev, pl172);

	/*
	 * Loop through each child node, which represent a chip select, and
	 * configure parameters and timing. If successful; populate devices
	 * under that node.
	 */
	for_each_available_child_of_node(np, child_np) {
		ret = pl172_parse_cs_config(adev, child_np);
		if (ret)
			continue;

		of_platform_populate(child_np, NULL, NULL, dev);
	}

	return 0;

err_no_ioremap:
	amba_release_regions(adev);
err_clk_enable:
	clk_disable_unprepare(pl172->clk);
	return ret;
}

static int pl172_remove(struct amba_device *adev)
{
	struct pl172_data *pl172 = amba_get_drvdata(adev);

	clk_disable_unprepare(pl172->clk);
	amba_release_regions(adev);

	return 0;
}

static const struct amba_id pl172_ids[] = {
	{
		.id	= 0x07341172,
		.mask	= 0xffffffff,
	},
	{ 0, 0 },
};
MODULE_DEVICE_TABLE(amba, pl172_ids);

static struct amba_driver pl172_driver = {
	.drv = {
		.name	= "memory-pl172",
	},
	.probe		= pl172_probe,
	.remove		= pl172_remove,
	.id_table	= pl172_ids,
};
module_amba_driver(pl172_driver);

MODULE_AUTHOR("Joachim Eastwood <manabian@gmail.com>");
MODULE_DESCRIPTION("PL172 Memory Controller Driver");
MODULE_LICENSE("GPL v2");
