// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Author: Yunfei Dong <yunfei.dong@mediatek.com>
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include "mtk_vcodec_drv.h"
#include "mtk_vcodec_dec.h"
#include "mtk_vcodec_dec_hw.h"
#include "mtk_vcodec_dec_pm.h"
#include "mtk_vcodec_intr.h"
#include "mtk_vcodec_util.h"

static const struct of_device_id mtk_vdec_hw_match[] = {
	{
		.compatible = "mediatek,mtk-vcodec-lat",
		.data = (void *)MTK_VDEC_LAT0,
	},
	{
		.compatible = "mediatek,mtk-vcodec-core",
		.data = (void *)MTK_VDEC_CORE,
	},
	{
		.compatible = "mediatek,mtk-vcodec-lat-soc",
		.data = (void *)MTK_VDEC_LAT_SOC,
	},
	{},
};
MODULE_DEVICE_TABLE(of, mtk_vdec_hw_match);

static int mtk_vdec_hw_prob_done(struct mtk_vcodec_dev *vdec_dev)
{
	struct platform_device *pdev = vdec_dev->plat_dev;
	struct device_node *subdev_node;
	enum mtk_vdec_hw_id hw_idx;
	const struct of_device_id *of_id;
	int i;

	for (i = 0; i < ARRAY_SIZE(mtk_vdec_hw_match); i++) {
		of_id = &mtk_vdec_hw_match[i];
		subdev_node = of_find_compatible_node(NULL, NULL,
						      of_id->compatible);
		if (!subdev_node)
			continue;

		of_node_put(subdev_node);

		hw_idx = (enum mtk_vdec_hw_id)(uintptr_t)of_id->data;
		if (!test_bit(hw_idx, vdec_dev->subdev_bitmap)) {
			dev_err(&pdev->dev, "vdec %d is not ready", hw_idx);
			return -EAGAIN;
		}
	}

	return 0;
}

static irqreturn_t mtk_vdec_hw_irq_handler(int irq, void *priv)
{
	struct mtk_vdec_hw_dev *dev = priv;
	struct mtk_vcodec_ctx *ctx;
	u32 cg_status;
	unsigned int dec_done_status;
	void __iomem *vdec_misc_addr = dev->reg_base[VDEC_HW_MISC] +
					VDEC_IRQ_CFG_REG;

	ctx = mtk_vcodec_get_curr_ctx(dev->main_dev, dev->hw_idx);

	/* check if HW active or not */
	cg_status = readl(dev->reg_base[VDEC_HW_SYS]);
	if (cg_status & VDEC_HW_ACTIVE) {
		mtk_v4l2_err("vdec active is not 0x0 (0x%08x)",
			     cg_status);
		return IRQ_HANDLED;
	}

	dec_done_status = readl(vdec_misc_addr);
	if ((dec_done_status & MTK_VDEC_IRQ_STATUS_DEC_SUCCESS) !=
	    MTK_VDEC_IRQ_STATUS_DEC_SUCCESS)
		return IRQ_HANDLED;

	/* clear interrupt */
	writel(dec_done_status | VDEC_IRQ_CFG, vdec_misc_addr);
	writel(dec_done_status & ~VDEC_IRQ_CLR, vdec_misc_addr);

	wake_up_ctx(ctx, MTK_INST_IRQ_RECEIVED, dev->hw_idx);

	mtk_v4l2_debug(3, "wake up ctx %d, dec_done_status=%x",
		       ctx->id, dec_done_status);

	return IRQ_HANDLED;
}

static int mtk_vdec_hw_init_irq(struct mtk_vdec_hw_dev *dev)
{
	struct platform_device *pdev = dev->plat_dev;
	int ret;

	dev->dec_irq = platform_get_irq(pdev, 0);
	if (dev->dec_irq < 0)
		return dev->dec_irq;

	irq_set_status_flags(dev->dec_irq, IRQ_NOAUTOEN);
	ret = devm_request_irq(&pdev->dev, dev->dec_irq,
			       mtk_vdec_hw_irq_handler, 0, pdev->name, dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to install dev->dec_irq %d (%d)",
			dev->dec_irq, ret);
		return ret;
	}

	return 0;
}

static int mtk_vdec_hw_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_vdec_hw_dev *subdev_dev;
	struct mtk_vcodec_dev *main_dev;
	const struct of_device_id *of_id;
	int hw_idx;
	int ret;

	if (!dev->parent) {
		dev_err(dev, "no parent for hardware devices.\n");
		return -ENODEV;
	}

	main_dev = dev_get_drvdata(dev->parent);
	if (!main_dev) {
		dev_err(dev, "failed to get parent driver data");
		return -EINVAL;
	}

	subdev_dev = devm_kzalloc(dev, sizeof(*subdev_dev), GFP_KERNEL);
	if (!subdev_dev)
		return -ENOMEM;

	subdev_dev->plat_dev = pdev;
	ret = mtk_vcodec_init_dec_clk(pdev, &subdev_dev->pm);
	if (ret)
		return ret;

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return ret;

	of_id = of_match_device(mtk_vdec_hw_match, dev);
	if (!of_id) {
		dev_err(dev, "Can't get vdec subdev id.\n");
		return -EINVAL;
	}

	hw_idx = (enum mtk_vdec_hw_id)(uintptr_t)of_id->data;
	if (hw_idx >= MTK_VDEC_HW_MAX) {
		dev_err(dev, "Hardware index %d not correct.\n", hw_idx);
		return -EINVAL;
	}

	main_dev->subdev_dev[hw_idx] = subdev_dev;
	subdev_dev->hw_idx = hw_idx;
	subdev_dev->main_dev = main_dev;
	subdev_dev->reg_base[VDEC_HW_SYS] = main_dev->reg_base[VDEC_HW_SYS];
	set_bit(subdev_dev->hw_idx, main_dev->subdev_bitmap);

	if (IS_SUPPORT_VDEC_HW_IRQ(hw_idx)) {
		ret = mtk_vdec_hw_init_irq(subdev_dev);
		if (ret)
			return ret;
	}

	subdev_dev->reg_base[VDEC_HW_MISC] =
		devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR((__force void *)subdev_dev->reg_base[VDEC_HW_MISC])) {
		ret = PTR_ERR((__force void *)subdev_dev->reg_base[VDEC_HW_MISC]);
		return ret;
	}

	if (!main_dev->subdev_prob_done)
		main_dev->subdev_prob_done = mtk_vdec_hw_prob_done;

	platform_set_drvdata(pdev, subdev_dev);
	return 0;
}

static struct platform_driver mtk_vdec_driver = {
	.probe	= mtk_vdec_hw_probe,
	.driver	= {
		.name	= "mtk-vdec-comp",
		.of_match_table = mtk_vdec_hw_match,
	},
};
module_platform_driver(mtk_vdec_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Mediatek video decoder hardware driver");
