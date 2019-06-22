/*
* Copyright (c) 2016 MediaTek Inc.
* Author: Tiffany Lin <tiffany.lin@mediatek.com>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/

#include <linux/clk.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <soc/mediatek/smi.h>

#include "mtk_vcodec_enc_pm.h"
#include "mtk_vcodec_util.h"
#include "mtk_vpu.h"


int mtk_vcodec_init_enc_pm(struct mtk_vcodec_dev *mtkdev)
{
	struct device_node *node;
	struct platform_device *pdev;
	struct device *dev;
	struct mtk_vcodec_pm *pm;
	int ret = 0;

	pdev = mtkdev->plat_dev;
	pm = &mtkdev->pm;
	memset(pm, 0, sizeof(struct mtk_vcodec_pm));
	pm->mtkdev = mtkdev;
	pm->dev = &pdev->dev;
	dev = &pdev->dev;

	node = of_parse_phandle(dev->of_node, "mediatek,larb", 0);
	if (!node) {
		mtk_v4l2_err("no mediatek,larb found");
		return -ENODEV;
	}
	pdev = of_find_device_by_node(node);
	of_node_put(node);
	if (!pdev) {
		mtk_v4l2_err("no mediatek,larb device found");
		return -ENODEV;
	}
	pm->larbvenc = &pdev->dev;

	node = of_parse_phandle(dev->of_node, "mediatek,larb", 1);
	if (!node) {
		mtk_v4l2_err("no mediatek,larb found");
		return -ENODEV;
	}

	pdev = of_find_device_by_node(node);
	of_node_put(node);
	if (!pdev) {
		mtk_v4l2_err("no mediatek,larb device found");
		return -ENODEV;
	}

	pm->larbvenclt = &pdev->dev;
	pdev = mtkdev->plat_dev;
	pm->dev = &pdev->dev;

	pm->vencpll_d2 = devm_clk_get(&pdev->dev, "venc_sel_src");
	if (IS_ERR(pm->vencpll_d2)) {
		mtk_v4l2_err("devm_clk_get vencpll_d2 fail");
		ret = PTR_ERR(pm->vencpll_d2);
	}

	pm->venc_sel = devm_clk_get(&pdev->dev, "venc_sel");
	if (IS_ERR(pm->venc_sel)) {
		mtk_v4l2_err("devm_clk_get venc_sel fail");
		ret = PTR_ERR(pm->venc_sel);
	}

	pm->univpll1_d2 = devm_clk_get(&pdev->dev, "venc_lt_sel_src");
	if (IS_ERR(pm->univpll1_d2)) {
		mtk_v4l2_err("devm_clk_get univpll1_d2 fail");
		ret = PTR_ERR(pm->univpll1_d2);
	}

	pm->venc_lt_sel = devm_clk_get(&pdev->dev, "venc_lt_sel");
	if (IS_ERR(pm->venc_lt_sel)) {
		mtk_v4l2_err("devm_clk_get venc_lt_sel fail");
		ret = PTR_ERR(pm->venc_lt_sel);
	}

	return ret;
}

void mtk_vcodec_release_enc_pm(struct mtk_vcodec_dev *mtkdev)
{
}


void mtk_vcodec_enc_clock_on(struct mtk_vcodec_pm *pm)
{
	int ret;

	ret = clk_prepare_enable(pm->venc_sel);
	if (ret)
		mtk_v4l2_err("clk_prepare_enable fail %d", ret);

	ret = clk_set_parent(pm->venc_sel, pm->vencpll_d2);
	if (ret)
		mtk_v4l2_err("clk_set_parent fail %d", ret);

	ret = clk_prepare_enable(pm->venc_lt_sel);
	if (ret)
		mtk_v4l2_err("clk_prepare_enable fail %d", ret);

	ret = clk_set_parent(pm->venc_lt_sel, pm->univpll1_d2);
	if (ret)
		mtk_v4l2_err("clk_set_parent fail %d", ret);

	ret = mtk_smi_larb_get(pm->larbvenc);
	if (ret)
		mtk_v4l2_err("mtk_smi_larb_get larb3 fail %d", ret);

	ret = mtk_smi_larb_get(pm->larbvenclt);
	if (ret)
		mtk_v4l2_err("mtk_smi_larb_get larb4 fail %d", ret);

}

void mtk_vcodec_enc_clock_off(struct mtk_vcodec_pm *pm)
{
	mtk_smi_larb_put(pm->larbvenc);
	mtk_smi_larb_put(pm->larbvenclt);
	clk_disable_unprepare(pm->venc_lt_sel);
	clk_disable_unprepare(pm->venc_sel);
}
