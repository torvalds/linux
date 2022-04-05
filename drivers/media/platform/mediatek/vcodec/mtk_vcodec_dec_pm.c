// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Tiffany Lin <tiffany.lin@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>

#include "mtk_vcodec_dec_hw.h"
#include "mtk_vcodec_dec_pm.h"
#include "mtk_vcodec_util.h"

int mtk_vcodec_init_dec_clk(struct platform_device *pdev, struct mtk_vcodec_pm *pm)
{
	struct mtk_vcodec_clk *dec_clk;
	struct mtk_vcodec_clk_info *clk_info;
	int i = 0, ret;

	dec_clk = &pm->vdec_clk;
	pm->dev = &pdev->dev;

	dec_clk->clk_num =
		of_property_count_strings(pdev->dev.of_node, "clock-names");
	if (dec_clk->clk_num > 0) {
		dec_clk->clk_info = devm_kcalloc(&pdev->dev,
			dec_clk->clk_num, sizeof(*clk_info),
			GFP_KERNEL);
		if (!dec_clk->clk_info)
			return -ENOMEM;
	} else {
		mtk_v4l2_err("Failed to get vdec clock count");
		return -EINVAL;
	}

	for (i = 0; i < dec_clk->clk_num; i++) {
		clk_info = &dec_clk->clk_info[i];
		ret = of_property_read_string_index(pdev->dev.of_node,
			"clock-names", i, &clk_info->clk_name);
		if (ret) {
			mtk_v4l2_err("Failed to get clock name id = %d", i);
			return ret;
		}
		clk_info->vcodec_clk = devm_clk_get(&pdev->dev,
			clk_info->clk_name);
		if (IS_ERR(clk_info->vcodec_clk)) {
			mtk_v4l2_err("devm_clk_get (%d)%s fail", i,
				clk_info->clk_name);
			return PTR_ERR(clk_info->vcodec_clk);
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_vcodec_init_dec_clk);

int mtk_vcodec_dec_pw_on(struct mtk_vcodec_dev *vdec_dev, int hw_idx)
{
	struct mtk_vdec_hw_dev *subdev_dev;
	struct mtk_vcodec_pm *pm;
	int ret;

	if (vdec_dev->vdec_pdata->is_subdev_supported) {
		subdev_dev = mtk_vcodec_get_hw_dev(vdec_dev, hw_idx);
		if (!subdev_dev) {
			mtk_v4l2_err("Failed to get hw dev\n");
			return -EINVAL;
		}
		pm = &subdev_dev->pm;
	} else {
		pm = &vdec_dev->pm;
	}

	ret = pm_runtime_resume_and_get(pm->dev);
	if (ret)
		mtk_v4l2_err("pm_runtime_resume_and_get fail %d", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(mtk_vcodec_dec_pw_on);

void mtk_vcodec_dec_pw_off(struct mtk_vcodec_dev *vdec_dev, int hw_idx)
{
	struct mtk_vdec_hw_dev *subdev_dev;
	struct mtk_vcodec_pm *pm;
	int ret;

	if (vdec_dev->vdec_pdata->is_subdev_supported) {
		subdev_dev = mtk_vcodec_get_hw_dev(vdec_dev, hw_idx);
		if (!subdev_dev) {
			mtk_v4l2_err("Failed to get hw dev\n");
			return;
		}
		pm = &subdev_dev->pm;
	} else {
		pm = &vdec_dev->pm;
	}

	ret = pm_runtime_put_sync(pm->dev);
	if (ret)
		mtk_v4l2_err("pm_runtime_put_sync fail %d", ret);
}
EXPORT_SYMBOL_GPL(mtk_vcodec_dec_pw_off);

void mtk_vcodec_dec_clock_on(struct mtk_vcodec_dev *vdec_dev, int hw_idx)
{
	struct mtk_vdec_hw_dev *subdev_dev;
	struct mtk_vcodec_pm *pm;
	struct mtk_vcodec_clk *dec_clk;
	int ret, i;

	if (vdec_dev->vdec_pdata->is_subdev_supported) {
		subdev_dev = mtk_vcodec_get_hw_dev(vdec_dev, hw_idx);
		if (!subdev_dev) {
			mtk_v4l2_err("Failed to get hw dev\n");
			return;
		}
		pm = &subdev_dev->pm;
		enable_irq(subdev_dev->dec_irq);
	} else {
		pm = &vdec_dev->pm;
		enable_irq(vdec_dev->dec_irq);
	}

	dec_clk = &pm->vdec_clk;
	for (i = 0; i < dec_clk->clk_num; i++) {
		ret = clk_prepare_enable(dec_clk->clk_info[i].vcodec_clk);
		if (ret) {
			mtk_v4l2_err("clk_prepare_enable %d %s fail %d", i,
				dec_clk->clk_info[i].clk_name, ret);
			goto error;
		}
	}

	return;
error:
	for (i -= 1; i >= 0; i--)
		clk_disable_unprepare(dec_clk->clk_info[i].vcodec_clk);
}
EXPORT_SYMBOL_GPL(mtk_vcodec_dec_clock_on);

void mtk_vcodec_dec_clock_off(struct mtk_vcodec_dev *vdec_dev, int hw_idx)
{
	struct mtk_vdec_hw_dev *subdev_dev;
	struct mtk_vcodec_pm *pm;
	struct mtk_vcodec_clk *dec_clk;
	int i;

	if (vdec_dev->vdec_pdata->is_subdev_supported) {
		subdev_dev = mtk_vcodec_get_hw_dev(vdec_dev, hw_idx);
		if (!subdev_dev) {
			mtk_v4l2_err("Failed to get hw dev\n");
			return;
		}
		pm = &subdev_dev->pm;
		disable_irq(subdev_dev->dec_irq);
	} else {
		pm = &vdec_dev->pm;
		disable_irq(vdec_dev->dec_irq);
	}

	dec_clk = &pm->vdec_clk;
	for (i = dec_clk->clk_num - 1; i >= 0; i--)
		clk_disable_unprepare(dec_clk->clk_info[i].vcodec_clk);
}
EXPORT_SYMBOL_GPL(mtk_vcodec_dec_clock_off);
