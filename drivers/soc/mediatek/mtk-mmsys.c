// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: James Liao <jamesjj.liao@mediatek.com>
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/soc/mediatek/mtk-mmsys.h>

#include "mtk-mmsys.h"
#include "mt8167-mmsys.h"
#include "mt8183-mmsys.h"
#include "mt8186-mmsys.h"
#include "mt8192-mmsys.h"
#include "mt8195-mmsys.h"
#include "mt8365-mmsys.h"

static const struct mtk_mmsys_driver_data mt2701_mmsys_driver_data = {
	.clk_driver = "clk-mt2701-mm",
	.routes = mmsys_default_routing_table,
	.num_routes = ARRAY_SIZE(mmsys_default_routing_table),
};

static const struct mtk_mmsys_match_data mt2701_mmsys_match_data = {
	.num_drv_data = 1,
	.drv_data = {
		&mt2701_mmsys_driver_data,
	},
};

static const struct mtk_mmsys_driver_data mt2712_mmsys_driver_data = {
	.clk_driver = "clk-mt2712-mm",
	.routes = mmsys_default_routing_table,
	.num_routes = ARRAY_SIZE(mmsys_default_routing_table),
};

static const struct mtk_mmsys_match_data mt2712_mmsys_match_data = {
	.num_drv_data = 1,
	.drv_data = {
		&mt2712_mmsys_driver_data,
	},
};

static const struct mtk_mmsys_driver_data mt6779_mmsys_driver_data = {
	.clk_driver = "clk-mt6779-mm",
};

static const struct mtk_mmsys_match_data mt6779_mmsys_match_data = {
	.num_drv_data = 1,
	.drv_data = {
		&mt6779_mmsys_driver_data,
	},
};

static const struct mtk_mmsys_driver_data mt6797_mmsys_driver_data = {
	.clk_driver = "clk-mt6797-mm",
};

static const struct mtk_mmsys_match_data mt6797_mmsys_match_data = {
	.num_drv_data = 1,
	.drv_data = {
		&mt6797_mmsys_driver_data,
	},
};

static const struct mtk_mmsys_driver_data mt8167_mmsys_driver_data = {
	.clk_driver = "clk-mt8167-mm",
	.routes = mt8167_mmsys_routing_table,
	.num_routes = ARRAY_SIZE(mt8167_mmsys_routing_table),
};

static const struct mtk_mmsys_match_data mt8167_mmsys_match_data = {
	.num_drv_data = 1,
	.drv_data = {
		&mt8167_mmsys_driver_data,
	},
};

static const struct mtk_mmsys_driver_data mt8173_mmsys_driver_data = {
	.clk_driver = "clk-mt8173-mm",
	.routes = mmsys_default_routing_table,
	.num_routes = ARRAY_SIZE(mmsys_default_routing_table),
	.sw0_rst_offset = MT8183_MMSYS_SW0_RST_B,
};

static const struct mtk_mmsys_match_data mt8173_mmsys_match_data = {
	.num_drv_data = 1,
	.drv_data = {
		&mt8173_mmsys_driver_data,
	},
};

static const struct mtk_mmsys_driver_data mt8183_mmsys_driver_data = {
	.clk_driver = "clk-mt8183-mm",
	.routes = mmsys_mt8183_routing_table,
	.num_routes = ARRAY_SIZE(mmsys_mt8183_routing_table),
	.sw0_rst_offset = MT8183_MMSYS_SW0_RST_B,
};

static const struct mtk_mmsys_match_data mt8183_mmsys_match_data = {
	.num_drv_data = 1,
	.drv_data = {
		&mt8183_mmsys_driver_data,
	},
};

static const struct mtk_mmsys_driver_data mt8186_mmsys_driver_data = {
	.clk_driver = "clk-mt8186-mm",
	.routes = mmsys_mt8186_routing_table,
	.num_routes = ARRAY_SIZE(mmsys_mt8186_routing_table),
	.sw0_rst_offset = MT8186_MMSYS_SW0_RST_B,
};

static const struct mtk_mmsys_match_data mt8186_mmsys_match_data = {
	.num_drv_data = 1,
	.drv_data = {
		&mt8186_mmsys_driver_data,
	},
};

static const struct mtk_mmsys_driver_data mt8192_mmsys_driver_data = {
	.clk_driver = "clk-mt8192-mm",
	.routes = mmsys_mt8192_routing_table,
	.num_routes = ARRAY_SIZE(mmsys_mt8192_routing_table),
	.sw0_rst_offset = MT8186_MMSYS_SW0_RST_B,
};

static const struct mtk_mmsys_match_data mt8192_mmsys_match_data = {
	.num_drv_data = 1,
	.drv_data = {
		&mt8192_mmsys_driver_data,
	},
};

static const struct mtk_mmsys_driver_data mt8195_vdosys0_driver_data = {
	.io_start = 0x1c01a000,
	.clk_driver = "clk-mt8195-vdo0",
	.routes = mmsys_mt8195_routing_table,
	.num_routes = ARRAY_SIZE(mmsys_mt8195_routing_table),
};

static const struct mtk_mmsys_driver_data mt8195_vdosys1_driver_data = {
	.io_start = 0x1c100000,
	.clk_driver = "clk-mt8195-vdo1",
};

static const struct mtk_mmsys_match_data mt8195_mmsys_match_data = {
	.num_drv_data = 2,
	.drv_data = {
		&mt8195_vdosys0_driver_data,
		&mt8195_vdosys1_driver_data,
	},
};

static const struct mtk_mmsys_driver_data mt8365_mmsys_driver_data = {
	.clk_driver = "clk-mt8365-mm",
	.routes = mt8365_mmsys_routing_table,
	.num_routes = ARRAY_SIZE(mt8365_mmsys_routing_table),
};

static const struct mtk_mmsys_match_data mt8365_mmsys_match_data = {
	.num_drv_data = 1,
	.drv_data = {
		&mt8365_mmsys_driver_data,
	},
};

struct mtk_mmsys {
	void __iomem *regs;
	const struct mtk_mmsys_driver_data *data;
	spinlock_t lock; /* protects mmsys_sw_rst_b reg */
	struct reset_controller_dev rcdev;
	phys_addr_t io_start;
};

static int mtk_mmsys_find_match_drvdata(struct mtk_mmsys *mmsys,
					const struct mtk_mmsys_match_data *match)
{
	int i;

	for (i = 0; i < match->num_drv_data; i++)
		if (mmsys->io_start == match->drv_data[i]->io_start)
			return i;

	return -EINVAL;
}

void mtk_mmsys_ddp_connect(struct device *dev,
			   enum mtk_ddp_comp_id cur,
			   enum mtk_ddp_comp_id next)
{
	struct mtk_mmsys *mmsys = dev_get_drvdata(dev);
	const struct mtk_mmsys_routes *routes = mmsys->data->routes;
	u32 reg;
	int i;

	for (i = 0; i < mmsys->data->num_routes; i++)
		if (cur == routes[i].from_comp && next == routes[i].to_comp) {
			reg = readl_relaxed(mmsys->regs + routes[i].addr);
			reg &= ~routes[i].mask;
			reg |= routes[i].val;
			writel_relaxed(reg, mmsys->regs + routes[i].addr);
		}
}
EXPORT_SYMBOL_GPL(mtk_mmsys_ddp_connect);

void mtk_mmsys_ddp_disconnect(struct device *dev,
			      enum mtk_ddp_comp_id cur,
			      enum mtk_ddp_comp_id next)
{
	struct mtk_mmsys *mmsys = dev_get_drvdata(dev);
	const struct mtk_mmsys_routes *routes = mmsys->data->routes;
	u32 reg;
	int i;

	for (i = 0; i < mmsys->data->num_routes; i++)
		if (cur == routes[i].from_comp && next == routes[i].to_comp) {
			reg = readl_relaxed(mmsys->regs + routes[i].addr);
			reg &= ~routes[i].mask;
			writel_relaxed(reg, mmsys->regs + routes[i].addr);
		}
}
EXPORT_SYMBOL_GPL(mtk_mmsys_ddp_disconnect);

static int mtk_mmsys_reset_update(struct reset_controller_dev *rcdev, unsigned long id,
				  bool assert)
{
	struct mtk_mmsys *mmsys = container_of(rcdev, struct mtk_mmsys, rcdev);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&mmsys->lock, flags);

	reg = readl_relaxed(mmsys->regs + mmsys->data->sw0_rst_offset);

	if (assert)
		reg &= ~BIT(id);
	else
		reg |= BIT(id);

	writel_relaxed(reg, mmsys->regs + mmsys->data->sw0_rst_offset);

	spin_unlock_irqrestore(&mmsys->lock, flags);

	return 0;
}

static int mtk_mmsys_reset_assert(struct reset_controller_dev *rcdev, unsigned long id)
{
	return mtk_mmsys_reset_update(rcdev, id, true);
}

static int mtk_mmsys_reset_deassert(struct reset_controller_dev *rcdev, unsigned long id)
{
	return mtk_mmsys_reset_update(rcdev, id, false);
}

static int mtk_mmsys_reset(struct reset_controller_dev *rcdev, unsigned long id)
{
	int ret;

	ret = mtk_mmsys_reset_assert(rcdev, id);
	if (ret)
		return ret;

	usleep_range(1000, 1100);

	return mtk_mmsys_reset_deassert(rcdev, id);
}

static const struct reset_control_ops mtk_mmsys_reset_ops = {
	.assert = mtk_mmsys_reset_assert,
	.deassert = mtk_mmsys_reset_deassert,
	.reset = mtk_mmsys_reset,
};

static int mtk_mmsys_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct platform_device *clks;
	struct platform_device *drm;
	const struct mtk_mmsys_match_data *match_data;
	struct mtk_mmsys *mmsys;
	struct resource *res;
	int ret;

	mmsys = devm_kzalloc(dev, sizeof(*mmsys), GFP_KERNEL);
	if (!mmsys)
		return -ENOMEM;

	mmsys->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mmsys->regs)) {
		ret = PTR_ERR(mmsys->regs);
		dev_err(dev, "Failed to ioremap mmsys registers: %d\n", ret);
		return ret;
	}

	spin_lock_init(&mmsys->lock);

	mmsys->rcdev.owner = THIS_MODULE;
	mmsys->rcdev.nr_resets = 32;
	mmsys->rcdev.ops = &mtk_mmsys_reset_ops;
	mmsys->rcdev.of_node = pdev->dev.of_node;
	ret = devm_reset_controller_register(&pdev->dev, &mmsys->rcdev);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't register mmsys reset controller: %d\n", ret);
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Couldn't get mmsys resource\n");
		return -EINVAL;
	}
	mmsys->io_start = res->start;

	match_data = of_device_get_match_data(dev);
	if (match_data->num_drv_data > 1) {
		/* This SoC has multiple mmsys channels */
		ret = mtk_mmsys_find_match_drvdata(mmsys, match_data);
		if (ret < 0) {
			dev_err(dev, "Couldn't get match driver data\n");
			return ret;
		}
		mmsys->data = match_data->drv_data[ret];
	} else {
		dev_dbg(dev, "Using single mmsys channel\n");
		mmsys->data = match_data->drv_data[0];
	}

	platform_set_drvdata(pdev, mmsys);

	clks = platform_device_register_data(&pdev->dev, mmsys->data->clk_driver,
					     PLATFORM_DEVID_AUTO, NULL, 0);
	if (IS_ERR(clks))
		return PTR_ERR(clks);

	drm = platform_device_register_data(&pdev->dev, "mediatek-drm",
					    PLATFORM_DEVID_AUTO, NULL, 0);
	if (IS_ERR(drm)) {
		platform_device_unregister(clks);
		return PTR_ERR(drm);
	}

	return 0;
}

static const struct of_device_id of_match_mtk_mmsys[] = {
	{
		.compatible = "mediatek,mt2701-mmsys",
		.data = &mt2701_mmsys_match_data,
	},
	{
		.compatible = "mediatek,mt2712-mmsys",
		.data = &mt2712_mmsys_match_data,
	},
	{
		.compatible = "mediatek,mt6779-mmsys",
		.data = &mt6779_mmsys_match_data,
	},
	{
		.compatible = "mediatek,mt6797-mmsys",
		.data = &mt6797_mmsys_match_data,
	},
	{
		.compatible = "mediatek,mt8167-mmsys",
		.data = &mt8167_mmsys_match_data,
	},
	{
		.compatible = "mediatek,mt8173-mmsys",
		.data = &mt8173_mmsys_match_data,
	},
	{
		.compatible = "mediatek,mt8183-mmsys",
		.data = &mt8183_mmsys_match_data,
	},
	{
		.compatible = "mediatek,mt8186-mmsys",
		.data = &mt8186_mmsys_match_data,
	},
	{
		.compatible = "mediatek,mt8192-mmsys",
		.data = &mt8192_mmsys_match_data,
	},
	{
		.compatible = "mediatek,mt8195-mmsys",
		.data = &mt8195_mmsys_match_data,
	},
	{
		.compatible = "mediatek,mt8365-mmsys",
		.data = &mt8365_mmsys_match_data,
	},
	{ }
};

static struct platform_driver mtk_mmsys_drv = {
	.driver = {
		.name = "mtk-mmsys",
		.of_match_table = of_match_mtk_mmsys,
	},
	.probe = mtk_mmsys_probe,
};

builtin_platform_driver(mtk_mmsys_drv);
