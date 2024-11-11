// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Copyright (c) 2024 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <dt-bindings/interconnect/mediatek,mt8183.h>

#include "icc-emi.h"

static struct mtk_icc_node ddr_emi = {
	.name = "ddr-emi",
	.id = SLAVE_DDR_EMI,
	.ep = 1,
};

static struct mtk_icc_node mcusys = {
	.name = "mcusys",
	.id = MASTER_MCUSYS,
	.ep = 0,
	.num_links = 1,
	.links = { SLAVE_DDR_EMI }
};

static struct mtk_icc_node gpu = {
	.name = "gpu",
	.id = MASTER_MFG,
	.ep = 0,
	.num_links = 1,
	.links = { SLAVE_DDR_EMI }
};

static struct mtk_icc_node mmsys = {
	.name = "mmsys",
	.id = MASTER_MMSYS,
	.ep = 0,
	.num_links = 1,
	.links = { SLAVE_DDR_EMI }
};

static struct mtk_icc_node mm_vpu = {
	.name = "mm-vpu",
	.id = MASTER_MM_VPU,
	.ep = 0,
	.num_links = 1,
	.links = { MASTER_MMSYS }
};

static struct mtk_icc_node mm_disp = {
	.name = "mm-disp",
	.id = MASTER_MM_DISP,
	.ep = 0,
	.num_links = 1,
	.links = { MASTER_MMSYS }
};

static struct mtk_icc_node mm_vdec = {
	.name = "mm-vdec",
	.id = MASTER_MM_VDEC,
	.ep = 0,
	.num_links = 1,
	.links = { MASTER_MMSYS }
};

static struct mtk_icc_node mm_venc = {
	.name = "mm-venc",
	.id = MASTER_MM_VENC,
	.ep = 0,
	.num_links = 1,
	.links = { MASTER_MMSYS }
};

static struct mtk_icc_node mm_cam = {
	.name = "mm-cam",
	.id = MASTER_MM_CAM,
	.ep = 0,
	.num_links = 1,
	.links = { MASTER_MMSYS }
};

static struct mtk_icc_node mm_img = {
	.name = "mm-img",
	.id = MASTER_MM_IMG,
	.ep = 0,
	.num_links = 1,
	.links = { MASTER_MMSYS }
};

static struct mtk_icc_node mm_mdp = {
	.name = "mm-mdp",
	.id = MASTER_MM_MDP,
	.ep = 0,
	.num_links = 1,
	.links = { MASTER_MMSYS }
};

static struct mtk_icc_node *mt8183_emi_icc_nodes[] = {
	[SLAVE_DDR_EMI] = &ddr_emi,
	[MASTER_MCUSYS] = &mcusys,
	[MASTER_MFG] = &gpu,
	[MASTER_MMSYS] = &mmsys,
	[MASTER_MM_VPU] = &mm_vpu,
	[MASTER_MM_DISP] = &mm_disp,
	[MASTER_MM_VDEC] = &mm_vdec,
	[MASTER_MM_VENC] = &mm_venc,
	[MASTER_MM_CAM] = &mm_cam,
	[MASTER_MM_IMG] = &mm_img,
	[MASTER_MM_MDP] = &mm_mdp
};

static const struct mtk_icc_desc mt8183_emi_icc = {
	.nodes = mt8183_emi_icc_nodes,
	.num_nodes = ARRAY_SIZE(mt8183_emi_icc_nodes),
};

static const struct of_device_id mtk_mt8183_emi_icc_of_match[] = {
	{ .compatible = "mediatek,mt8183-emi", .data = &mt8183_emi_icc },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mtk_mt8183_emi_icc_of_match);

static struct platform_driver mtk_emi_icc_mt8183_driver = {
	.driver = {
		.name = "emi-icc-mt8183",
		.of_match_table = mtk_mt8183_emi_icc_of_match,
		.sync_state = icc_sync_state,
	},
	.probe = mtk_emi_icc_probe,
	.remove = mtk_emi_icc_remove,

};
module_platform_driver(mtk_emi_icc_mt8183_driver);

MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_DESCRIPTION("MediaTek MT8183 EMI ICC driver");
MODULE_LICENSE("GPL");
