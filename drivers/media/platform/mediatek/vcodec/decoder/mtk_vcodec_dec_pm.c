// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Tiffany Lin <tiffany.lin@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>

#include "mtk_vcodec_dec_hw.h"
#include "mtk_vcodec_dec_pm.h"

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
		dev_err(&pdev->dev, "Failed to get vdec clock count");
		return -EINVAL;
	}

	for (i = 0; i < dec_clk->clk_num; i++) {
		clk_info = &dec_clk->clk_info[i];
		ret = of_property_read_string_index(pdev->dev.of_node,
			"clock-names", i, &clk_info->clk_name);
		if (ret) {
			dev_err(&pdev->dev, "Failed to get clock name id = %d", i);
			return ret;
		}
		clk_info->vcodec_clk = devm_clk_get(&pdev->dev,
			clk_info->clk_name);
		if (IS_ERR(clk_info->vcodec_clk)) {
			dev_err(&pdev->dev, "devm_clk_get (%d)%s fail", i, clk_info->clk_name);
			return PTR_ERR(clk_info->vcodec_clk);
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_vcodec_init_dec_clk);

static int mtk_vcodec_dec_pw_on(struct mtk_vcodec_pm *pm)
{
	int ret;

	ret = pm_runtime_resume_and_get(pm->dev);
	if (ret)
		dev_err(pm->dev, "pm_runtime_resume_and_get fail %d", ret);

	return ret;
}

static void mtk_vcodec_dec_pw_off(struct mtk_vcodec_pm *pm)
{
	int ret;

	ret = pm_runtime_put(pm->dev);
	if (ret && ret != -EAGAIN)
		dev_err(pm->dev, "pm_runtime_put fail %d", ret);
}

static void mtk_vcodec_dec_clock_on(struct mtk_vcodec_pm *pm)
{
	struct mtk_vcodec_clk *dec_clk;
	int ret, i;

	dec_clk = &pm->vdec_clk;
	for (i = 0; i < dec_clk->clk_num; i++) {
		ret = clk_prepare_enable(dec_clk->clk_info[i].vcodec_clk);
		if (ret) {
			dev_err(pm->dev, "clk_prepare_enable %d %s fail %d", i,
				dec_clk->clk_info[i].clk_name, ret);
			goto error;
		}
	}

	return;
error:
	for (i -= 1; i >= 0; i--)
		clk_disable_unprepare(dec_clk->clk_info[i].vcodec_clk);
}

static void mtk_vcodec_dec_clock_off(struct mtk_vcodec_pm *pm)
{
	struct mtk_vcodec_clk *dec_clk;
	int i;

	dec_clk = &pm->vdec_clk;
	for (i = dec_clk->clk_num - 1; i >= 0; i--)
		clk_disable_unprepare(dec_clk->clk_info[i].vcodec_clk);
}

static void mtk_vcodec_dec_enable_irq(struct mtk_vcodec_dec_dev *vdec_dev, int hw_idx)
{
	struct mtk_vdec_hw_dev *subdev_dev;

	if (!test_bit(hw_idx, vdec_dev->subdev_bitmap))
		return;

	if (vdec_dev->vdec_pdata->is_subdev_supported) {
		subdev_dev = mtk_vcodec_get_hw_dev(vdec_dev, hw_idx);
		if (subdev_dev)
			enable_irq(subdev_dev->dec_irq);
		else
			dev_err(&vdec_dev->plat_dev->dev, "Failed to get hw dev\n");
	} else {
		enable_irq(vdec_dev->dec_irq);
	}
}

static void mtk_vcodec_dec_disable_irq(struct mtk_vcodec_dec_dev *vdec_dev, int hw_idx)
{
	struct mtk_vdec_hw_dev *subdev_dev;

	if (!test_bit(hw_idx, vdec_dev->subdev_bitmap))
		return;

	if (vdec_dev->vdec_pdata->is_subdev_supported) {
		subdev_dev = mtk_vcodec_get_hw_dev(vdec_dev, hw_idx);
		if (subdev_dev)
			disable_irq(subdev_dev->dec_irq);
		else
			dev_err(&vdec_dev->plat_dev->dev, "Failed to get hw dev\n");
	} else {
		disable_irq(vdec_dev->dec_irq);
	}
}

static void mtk_vcodec_load_racing_info(struct mtk_vcodec_dec_ctx *ctx)
{
	void __iomem *vdec_racing_addr;
	int j;

	mutex_lock(&ctx->dev->dec_racing_info_mutex);
	if (atomic_inc_return(&ctx->dev->dec_active_cnt) == 1) {
		vdec_racing_addr = ctx->dev->reg_base[VDEC_MISC] + 0x100;
		for (j = 0; j < 132; j++)
			writel(ctx->dev->vdec_racing_info[j], vdec_racing_addr + j * 4);
	}
	mutex_unlock(&ctx->dev->dec_racing_info_mutex);
}

static void mtk_vcodec_record_racing_info(struct mtk_vcodec_dec_ctx *ctx)
{
	void __iomem *vdec_racing_addr;
	int j;

	mutex_lock(&ctx->dev->dec_racing_info_mutex);
	if (atomic_dec_and_test(&ctx->dev->dec_active_cnt)) {
		vdec_racing_addr = ctx->dev->reg_base[VDEC_MISC] + 0x100;
		for (j = 0; j < 132; j++)
			ctx->dev->vdec_racing_info[j] = readl(vdec_racing_addr + j * 4);
	}
	mutex_unlock(&ctx->dev->dec_racing_info_mutex);
}

static struct mtk_vcodec_pm *mtk_vcodec_dec_get_pm(struct mtk_vcodec_dec_dev *vdec_dev,
						   int hw_idx)
{
	struct mtk_vdec_hw_dev *subdev_dev;

	if (!test_bit(hw_idx, vdec_dev->subdev_bitmap))
		return NULL;

	if (vdec_dev->vdec_pdata->is_subdev_supported) {
		subdev_dev = mtk_vcodec_get_hw_dev(vdec_dev, hw_idx);
		if (subdev_dev)
			return &subdev_dev->pm;

		dev_err(&vdec_dev->plat_dev->dev, "Failed to get hw dev\n");
		return NULL;
	}

	return &vdec_dev->pm;
}

static void mtk_vcodec_dec_child_dev_on(struct mtk_vcodec_dec_dev *vdec_dev,
					int hw_idx)
{
	struct mtk_vcodec_pm *pm;

	pm = mtk_vcodec_dec_get_pm(vdec_dev, hw_idx);
	if (pm) {
		mtk_vcodec_dec_pw_on(pm);
		mtk_vcodec_dec_clock_on(pm);
	}

	if (hw_idx == MTK_VDEC_LAT0) {
		pm = mtk_vcodec_dec_get_pm(vdec_dev, MTK_VDEC_LAT_SOC);
		if (pm) {
			mtk_vcodec_dec_pw_on(pm);
			mtk_vcodec_dec_clock_on(pm);
		}
	}
}

static void mtk_vcodec_dec_child_dev_off(struct mtk_vcodec_dec_dev *vdec_dev,
					 int hw_idx)
{
	struct mtk_vcodec_pm *pm;

	pm = mtk_vcodec_dec_get_pm(vdec_dev, hw_idx);
	if (pm) {
		mtk_vcodec_dec_clock_off(pm);
		mtk_vcodec_dec_pw_off(pm);
	}

	if (hw_idx == MTK_VDEC_LAT0) {
		pm = mtk_vcodec_dec_get_pm(vdec_dev, MTK_VDEC_LAT_SOC);
		if (pm) {
			mtk_vcodec_dec_clock_off(pm);
			mtk_vcodec_dec_pw_off(pm);
		}
	}
}

void mtk_vcodec_dec_enable_hardware(struct mtk_vcodec_dec_ctx *ctx, int hw_idx)
{
	mutex_lock(&ctx->dev->dec_mutex[hw_idx]);

	if (IS_VDEC_LAT_ARCH(ctx->dev->vdec_pdata->hw_arch) &&
	    hw_idx == MTK_VDEC_CORE)
		mtk_vcodec_dec_child_dev_on(ctx->dev, MTK_VDEC_LAT0);
	mtk_vcodec_dec_child_dev_on(ctx->dev, hw_idx);

	mtk_vcodec_dec_enable_irq(ctx->dev, hw_idx);

	if (IS_VDEC_INNER_RACING(ctx->dev->dec_capability))
		mtk_vcodec_load_racing_info(ctx);
}
EXPORT_SYMBOL_GPL(mtk_vcodec_dec_enable_hardware);

void mtk_vcodec_dec_disable_hardware(struct mtk_vcodec_dec_ctx *ctx, int hw_idx)
{
	if (IS_VDEC_INNER_RACING(ctx->dev->dec_capability))
		mtk_vcodec_record_racing_info(ctx);

	mtk_vcodec_dec_disable_irq(ctx->dev, hw_idx);

	mtk_vcodec_dec_child_dev_off(ctx->dev, hw_idx);
	if (IS_VDEC_LAT_ARCH(ctx->dev->vdec_pdata->hw_arch) &&
	    hw_idx == MTK_VDEC_CORE)
		mtk_vcodec_dec_child_dev_off(ctx->dev, MTK_VDEC_LAT0);

	mutex_unlock(&ctx->dev->dec_mutex[hw_idx]);
}
EXPORT_SYMBOL_GPL(mtk_vcodec_dec_disable_hardware);
