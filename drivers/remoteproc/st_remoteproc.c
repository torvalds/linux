/*
 * ST's Remote Processor Control Driver
 *
 * Copyright (C) 2015 STMicroelectronics - All Rights Reserved
 *
 * Author: Ludovic Barre <ludovic.barre@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/remoteproc.h>
#include <linux/reset.h>

struct st_rproc_config {
	bool			sw_reset;
	bool			pwr_reset;
	unsigned long		bootaddr_mask;
};

struct st_rproc {
	struct st_rproc_config	*config;
	struct reset_control	*sw_reset;
	struct reset_control	*pwr_reset;
	struct clk		*clk;
	u32			clk_rate;
	struct regmap		*boot_base;
	u32			boot_offset;
};

static int st_rproc_start(struct rproc *rproc)
{
	struct st_rproc *ddata = rproc->priv;
	int err;

	regmap_update_bits(ddata->boot_base, ddata->boot_offset,
			   ddata->config->bootaddr_mask, rproc->bootaddr);

	err = clk_enable(ddata->clk);
	if (err) {
		dev_err(&rproc->dev, "Failed to enable clock\n");
		return err;
	}

	if (ddata->config->sw_reset) {
		err = reset_control_deassert(ddata->sw_reset);
		if (err) {
			dev_err(&rproc->dev, "Failed to deassert S/W Reset\n");
			goto sw_reset_fail;
		}
	}

	if (ddata->config->pwr_reset) {
		err = reset_control_deassert(ddata->pwr_reset);
		if (err) {
			dev_err(&rproc->dev, "Failed to deassert Power Reset\n");
			goto pwr_reset_fail;
		}
	}

	dev_info(&rproc->dev, "Started from 0x%x\n", rproc->bootaddr);

	return 0;


pwr_reset_fail:
	if (ddata->config->pwr_reset)
		reset_control_assert(ddata->sw_reset);
sw_reset_fail:
	clk_disable(ddata->clk);

	return err;
}

static int st_rproc_stop(struct rproc *rproc)
{
	struct st_rproc *ddata = rproc->priv;
	int sw_err = 0, pwr_err = 0;

	if (ddata->config->sw_reset) {
		sw_err = reset_control_assert(ddata->sw_reset);
		if (sw_err)
			dev_err(&rproc->dev, "Failed to assert S/W Reset\n");
	}

	if (ddata->config->pwr_reset) {
		pwr_err = reset_control_assert(ddata->pwr_reset);
		if (pwr_err)
			dev_err(&rproc->dev, "Failed to assert Power Reset\n");
	}

	clk_disable(ddata->clk);

	return sw_err ?: pwr_err;
}

static struct rproc_ops st_rproc_ops = {
	.start		= st_rproc_start,
	.stop		= st_rproc_stop,
};

/*
 * Fetch state of the processor: 0 is off, 1 is on.
 */
static int st_rproc_state(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct st_rproc *ddata = rproc->priv;
	int reset_sw = 0, reset_pwr = 0;

	if (ddata->config->sw_reset)
		reset_sw = reset_control_status(ddata->sw_reset);

	if (ddata->config->pwr_reset)
		reset_pwr = reset_control_status(ddata->pwr_reset);

	if (reset_sw < 0 || reset_pwr < 0)
		return -EINVAL;

	return !reset_sw && !reset_pwr;
}

static const struct st_rproc_config st40_rproc_cfg = {
	.sw_reset = true,
	.pwr_reset = true,
	.bootaddr_mask = GENMASK(28, 1),
};

static const struct st_rproc_config st231_rproc_cfg = {
	.sw_reset = true,
	.pwr_reset = false,
	.bootaddr_mask = GENMASK(31, 6),
};

static const struct of_device_id st_rproc_match[] = {
	{ .compatible = "st,st40-rproc", .data = &st40_rproc_cfg },
	{ .compatible = "st,st231-rproc", .data = &st231_rproc_cfg },
	{},
};
MODULE_DEVICE_TABLE(of, st_rproc_match);

static int st_rproc_parse_dt(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct st_rproc *ddata = rproc->priv;
	struct device_node *np = dev->of_node;
	int err;

	if (ddata->config->sw_reset) {
		ddata->sw_reset = devm_reset_control_get(dev, "sw_reset");
		if (IS_ERR(ddata->sw_reset)) {
			dev_err(dev, "Failed to get S/W Reset\n");
			return PTR_ERR(ddata->sw_reset);
		}
	}

	if (ddata->config->pwr_reset) {
		ddata->pwr_reset = devm_reset_control_get(dev, "pwr_reset");
		if (IS_ERR(ddata->pwr_reset)) {
			dev_err(dev, "Failed to get Power Reset\n");
			return PTR_ERR(ddata->pwr_reset);
		}
	}

	ddata->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(ddata->clk)) {
		dev_err(dev, "Failed to get clock\n");
		return PTR_ERR(ddata->clk);
	}

	err = of_property_read_u32(np, "clock-frequency", &ddata->clk_rate);
	if (err) {
		dev_err(dev, "failed to get clock frequency\n");
		return err;
	}

	ddata->boot_base = syscon_regmap_lookup_by_phandle(np, "st,syscfg");
	if (IS_ERR(ddata->boot_base)) {
		dev_err(dev, "Boot base not found\n");
		return PTR_ERR(ddata->boot_base);
	}

	err = of_property_read_u32_index(np, "st,syscfg", 1,
					 &ddata->boot_offset);
	if (err) {
		dev_err(dev, "Boot offset not found\n");
		return -EINVAL;
	}

	err = of_reserved_mem_device_init(dev);
	if (err) {
		dev_err(dev, "Failed to obtain shared memory\n");
		return err;
	}

	err = clk_prepare(ddata->clk);
	if (err)
		dev_err(dev, "failed to get clock\n");

	return err;
}

static int st_rproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct st_rproc *ddata;
	struct device_node *np = dev->of_node;
	struct rproc *rproc;
	int enabled;
	int ret;

	match = of_match_device(st_rproc_match, dev);
	if (!match || !match->data) {
		dev_err(dev, "No device match found\n");
		return -ENODEV;
	}

	rproc = rproc_alloc(dev, np->name, &st_rproc_ops, NULL, sizeof(*ddata));
	if (!rproc)
		return -ENOMEM;

	rproc->has_iommu = false;
	ddata = rproc->priv;
	ddata->config = (struct st_rproc_config *)match->data;

	platform_set_drvdata(pdev, rproc);

	ret = st_rproc_parse_dt(pdev);
	if (ret)
		goto free_rproc;

	enabled = st_rproc_state(pdev);
	if (enabled < 0)
		goto free_rproc;

	if (enabled) {
		atomic_inc(&rproc->power);
		rproc->state = RPROC_RUNNING;
	} else {
		clk_set_rate(ddata->clk, ddata->clk_rate);
	}

	ret = rproc_add(rproc);
	if (ret)
		goto free_rproc;

	return 0;

free_rproc:
	rproc_free(rproc);
	return ret;
}

static int st_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct st_rproc *ddata = rproc->priv;

	rproc_del(rproc);

	clk_disable_unprepare(ddata->clk);

	of_reserved_mem_device_release(&pdev->dev);

	rproc_free(rproc);

	return 0;
}

static struct platform_driver st_rproc_driver = {
	.probe = st_rproc_probe,
	.remove = st_rproc_remove,
	.driver = {
		.name = "st-rproc",
		.of_match_table = of_match_ptr(st_rproc_match),
	},
};
module_platform_driver(st_rproc_driver);

MODULE_DESCRIPTION("ST Remote Processor Control Driver");
MODULE_AUTHOR("Ludovic Barre <ludovic.barre@st.com>");
MODULE_LICENSE("GPL v2");
