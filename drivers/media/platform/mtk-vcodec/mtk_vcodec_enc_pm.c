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

#include "mtk_vcodec_enc_pm.h"
#include "mtk_vcodec_util.h"

int mtk_vcodec_init_enc_pm(struct mtk_vcodec_dev *mtkdev)
{
	struct device_node *node;
	struct platform_device *pdev;
	struct mtk_vcodec_pm *pm;
	struct mtk_vcodec_clk *enc_clk;
	struct mtk_vcodec_clk_info *clk_info;
	int ret = 0, i = 0;
	struct device *dev;

	pdev = mtkdev->plat_dev;
	pm = &mtkdev->pm;
	memset(pm, 0, sizeof(struct mtk_vcodec_pm));
	pm->mtkdev = mtkdev;
	pm->dev = &pdev->dev;
	dev = &pdev->dev;
	enc_clk = &pm->venc_clk;

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

	enc_clk->clk_num = of_property_count_strings(pdev->dev.of_node,
		"clock-names");
	if (enc_clk->clk_num > 0) {
		enc_clk->clk_info = devm_kcalloc(&pdev->dev,
			enc_clk->clk_num, sizeof(*clk_info),
			GFP_KERNEL);
		if (!enc_clk->clk_info)
			return -ENOMEM;
	} else {
		mtk_v4l2_err("Failed to get venc clock count");
		return -EINVAL;
	}

	for (i = 0; i < enc_clk->clk_num; i++) {
		clk_info = &enc_clk->clk_info[i];
		ret = of_property_read_string_index(pdev->dev.of_node,
			"clock-names", i, &clk_info->clk_name);
		if (ret) {
			mtk_v4l2_err("venc failed to get clk name %d", i);
			return ret;
		}
		clk_info->vcodec_clk = devm_clk_get(&pdev->dev,
			clk_info->clk_name);
		if (IS_ERR(clk_info->vcodec_clk)) {
			mtk_v4l2_err("venc devm_clk_get (%d)%s fail", i,
				clk_info->clk_name);
			return PTR_ERR(clk_info->vcodec_clk);
		}
	}

	return ret;
}

void mtk_vcodec_release_enc_pm(struct mtk_vcodec_dev *mtkdev)
{
}


void mtk_vcodec_enc_clock_on(struct mtk_vcodec_pm *pm)
{
	struct mtk_vcodec_clk *enc_clk = &pm->venc_clk;
	int ret, i = 0;

	for (i = 0; i < enc_clk->clk_num; i++) {
		ret = clk_prepare_enable(enc_clk->clk_info[i].vcodec_clk);
		if (ret) {
			mtk_v4l2_err("venc clk_prepare_enable %d %s fail %d", i,
				enc_clk->clk_info[i].clk_name, ret);
			goto clkerr;
		}
	}

	ret = mtk_smi_larb_get(pm->larbvenc);
	if (ret) {
		mtk_v4l2_err("mtk_smi_larb_get larb3 fail %d", ret);
		goto larbvencerr;
	}
	ret = mtk_smi_larb_get(pm->larbvenclt);
	if (ret) {
		mtk_v4l2_err("mtk_smi_larb_get larb4 fail %d", ret);
		goto larbvenclterr;
	}
	return;

larbvenclterr:
	mtk_smi_larb_put(pm->larbvenc);
larbvencerr:
clkerr:
	for (i -= 1; i >= 0; i--)
		clk_disable_unprepare(enc_clk->clk_info[i].vcodec_clk);
}

void mtk_vcodec_enc_clock_off(struct mtk_vcodec_pm *pm)
{
	struct mtk_vcodec_clk *enc_clk = &pm->venc_clk;
	int i = 0;

	mtk_smi_larb_put(pm->larbvenc);
	mtk_smi_larb_put(pm->larbvenclt);
	for (i = enc_clk->clk_num - 1; i >= 0; i--)
		clk_disable_unprepare(enc_clk->clk_info[i].vcodec_clk);
}
