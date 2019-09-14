// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Tiffany Lin <tiffany.lin@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <soc/mediatek/smi.h>

#include "mtk_vcodec_dec_pm.h"
#include "mtk_vcodec_util.h"
#include "mtk_vpu.h"

int mtk_vcodec_init_dec_pm(struct mtk_vcodec_dev *mtkdev)
{
	struct device_node *node;
	struct platform_device *pdev;
	struct mtk_vcodec_pm *pm;
	struct mtk_vcodec_clk *dec_clk;
	struct mtk_vcodec_clk_info *clk_info;
	int i = 0, ret = 0;

	pdev = mtkdev->plat_dev;
	pm = &mtkdev->pm;
	pm->mtkdev = mtkdev;
	dec_clk = &pm->vdec_clk;
	node = of_parse_phandle(pdev->dev.of_node, "mediatek,larb", 0);
	if (!node) {
		mtk_v4l2_err("of_parse_phandle mediatek,larb fail!");
		return -1;
	}

	pdev = of_find_device_by_node(node);
	of_node_put(node);
	if (WARN_ON(!pdev)) {
		return -1;
	}
	pm->larbvdec = &pdev->dev;
	pdev = mtkdev->plat_dev;
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

	pm_runtime_enable(&pdev->dev);

	return ret;
}

void mtk_vcodec_release_dec_pm(struct mtk_vcodec_dev *dev)
{
	pm_runtime_disable(dev->pm.dev);
}

void mtk_vcodec_dec_pw_on(struct mtk_vcodec_pm *pm)
{
	int ret;

	ret = pm_runtime_get_sync(pm->dev);
	if (ret)
		mtk_v4l2_err("pm_runtime_get_sync fail %d", ret);
}

void mtk_vcodec_dec_pw_off(struct mtk_vcodec_pm *pm)
{
	int ret;

	ret = pm_runtime_put_sync(pm->dev);
	if (ret)
		mtk_v4l2_err("pm_runtime_put_sync fail %d", ret);
}

void mtk_vcodec_dec_clock_on(struct mtk_vcodec_pm *pm)
{
	struct mtk_vcodec_clk *dec_clk = &pm->vdec_clk;
	int ret, i = 0;

	for (i = 0; i < dec_clk->clk_num; i++) {
		ret = clk_prepare_enable(dec_clk->clk_info[i].vcodec_clk);
		if (ret) {
			mtk_v4l2_err("clk_prepare_enable %d %s fail %d", i,
				dec_clk->clk_info[i].clk_name, ret);
			goto error;
		}
	}

	ret = mtk_smi_larb_get(pm->larbvdec);
	if (ret) {
		mtk_v4l2_err("mtk_smi_larb_get larbvdec fail %d", ret);
		goto error;
	}
	return;

error:
	for (i -= 1; i >= 0; i--)
		clk_disable_unprepare(dec_clk->clk_info[i].vcodec_clk);
}

void mtk_vcodec_dec_clock_off(struct mtk_vcodec_pm *pm)
{
	struct mtk_vcodec_clk *dec_clk = &pm->vdec_clk;
	int i = 0;

	mtk_smi_larb_put(pm->larbvdec);
	for (i = dec_clk->clk_num - 1; i >= 0; i--)
		clk_disable_unprepare(dec_clk->clk_info[i].vcodec_clk);
}
