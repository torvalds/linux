// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <dt-bindings/interconnect/mediatek,mt8196.h>

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

static struct mtk_icc_node mcu_port0 = {
	.name = "mcu-port0",
	.id = MASTER_MCU_0,
	.ep = 0,
	.num_links = 1,
	.links = { SLAVE_DDR_EMI }
};

static struct mtk_icc_node mcu_port1 = {
	.name = "mcu-port1",
	.id = MASTER_MCU_1,
	.ep = 0,
	.num_links = 1,
	.links = { SLAVE_DDR_EMI }
};

static struct mtk_icc_node mcu_port2 = {
	.name = "mcu-port2",
	.id = MASTER_MCU_2,
	.ep = 0,
	.num_links = 1,
	.links = { SLAVE_DDR_EMI }
};

static struct mtk_icc_node mcu_port3 = {
	.name = "mcu-port3",
	.id = MASTER_MCU_3,
	.ep = 0,
	.num_links = 1,
	.links = { SLAVE_DDR_EMI }
};

static struct mtk_icc_node mcu_port4 = {
	.name = "mcu-port4",
	.id = MASTER_MCU_4,
	.ep = 0,
	.num_links = 1,
	.links = { SLAVE_DDR_EMI }
};

static struct mtk_icc_node gpu = {
	.name = "gpu",
	.id = MASTER_GPUSYS,
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

static struct mtk_icc_node vpusys = {
	.name = "vpusys",
	.id = MASTER_VPUSYS,
	.ep = 0,
	.num_links = 1,
	.links = { SLAVE_DDR_EMI }
};

static struct mtk_icc_node vpu_port0 = {
	.name = "vpu-port0",
	.id = MASTER_VPU_0,
	.ep = 0,
	.num_links = 1,
	.links = { MASTER_VPUSYS }
};

static struct mtk_icc_node vpu_port1 = {
	.name = "vpu-port1",
	.id = MASTER_VPU_1,
	.ep = 0,
	.num_links = 1,
	.links = { MASTER_VPUSYS }
};

static struct mtk_icc_node mdlasys = {
	.name = "mdlasys",
	.id = MASTER_MDLASYS,
	.ep = 0,
	.num_links = 1,
	.links = { SLAVE_DDR_EMI }
};

static struct mtk_icc_node mdla_port0 = {
	.name = "mdla-port0",
	.id = MASTER_MDLA_0,
	.ep = 0,
	.num_links = 1,
	.links = { MASTER_MDLASYS }
};

static struct mtk_icc_node ufs = {
	.name = "ufs",
	.id = MASTER_UFS,
	.ep = 0,
	.num_links = 1,
	.links = { SLAVE_DDR_EMI }
};

static struct mtk_icc_node pcie = {
	.name = "pcie",
	.id = MASTER_PCIE,
	.ep = 0,
	.num_links = 1,
	.links = { SLAVE_DDR_EMI }
};

static struct mtk_icc_node usb = {
	.name = "usb",
	.id = MASTER_USB,
	.ep = 0,
	.num_links = 1,
	.links = { SLAVE_DDR_EMI }
};

static struct mtk_icc_node wifi = {
	.name = "wifi",
	.id = MASTER_WIFI,
	.ep = 0,
	.num_links = 1,
	.links = { SLAVE_DDR_EMI }
};

static struct mtk_icc_node bt = {
	.name = "bt",
	.id = MASTER_BT,
	.ep = 0,
	.num_links = 1,
	.links = { SLAVE_DDR_EMI }
};

static struct mtk_icc_node netsys = {
	.name = "netsys",
	.id = MASTER_NETSYS,
	.ep = 0,
	.num_links = 1,
	.links = { SLAVE_DDR_EMI }
};

static struct mtk_icc_node dbgif = {
	.name = "dbgif",
	.id = MASTER_DBGIF,
	.ep = 0,
	.num_links = 1,
	.links = { SLAVE_DDR_EMI }
};

static struct mtk_icc_node hrt_ddr_emi = {
	.name = "hrt-ddr-emi",
	.id = SLAVE_HRT_DDR_EMI,
	.ep = 2,
};

static struct mtk_icc_node hrt_mmsys = {
	.name = "hrt-mmsys",
	.id = MASTER_HRT_MMSYS,
	.ep = 0,
	.num_links = 1,
	.links = { SLAVE_HRT_DDR_EMI }
};

static struct mtk_icc_node hrt_mm_disp = {
	.name = "hrt-mm-disp",
	.id = MASTER_HRT_MM_DISP,
	.ep = 0,
	.num_links = 1,
	.links = { MASTER_HRT_MMSYS }
};

static struct mtk_icc_node hrt_mm_vdec = {
	.name = "hrt-mm-vdec",
	.id = MASTER_HRT_MM_VDEC,
	.ep = 0,
	.num_links = 1,
	.links = { MASTER_HRT_MMSYS }
};

static struct mtk_icc_node hrt_mm_venc = {
	.name = "hrt-mm-venc",
	.id = MASTER_HRT_MM_VENC,
	.ep = 0,
	.num_links = 1,
	.links = { MASTER_HRT_MMSYS }
};

static struct mtk_icc_node hrt_mm_cam = {
	.name = "hrt-mm-cam",
	.id = MASTER_HRT_MM_CAM,
	.ep = 0,
	.num_links = 1,
	.links = { MASTER_HRT_MMSYS }
};

static struct mtk_icc_node hrt_mm_img = {
	.name = "hrt-mm-img",
	.id = MASTER_HRT_MM_IMG,
	.ep = 0,
	.num_links = 1,
	.links = { MASTER_HRT_MMSYS }
};

static struct mtk_icc_node hrt_mm_mdp = {
	.name = "hrt-mm-mdp",
	.id = MASTER_HRT_MM_MDP,
	.ep = 0,
	.num_links = 1,
	.links = { MASTER_HRT_MMSYS }
};

static struct mtk_icc_node hrt_adsp = {
	.name = "hrt-adsp",
	.id = MASTER_HRT_ADSP,
	.ep = 0,
	.num_links = 1,
	.links = { SLAVE_HRT_DDR_EMI }
};

static struct mtk_icc_node hrt_dbgif = {
	.name = "hrt-dbgif",
	.id = MASTER_HRT_DBGIF,
	.ep = 0,
	.num_links = 1,
	.links = { SLAVE_HRT_DDR_EMI }
};

static struct mtk_icc_node *mt8196_emi_icc_nodes[] = {
	[SLAVE_DDR_EMI] = &ddr_emi,
	[MASTER_MCUSYS] = &mcusys,
	[MASTER_MCU_0] = &mcu_port0,
	[MASTER_MCU_1] = &mcu_port1,
	[MASTER_MCU_2] = &mcu_port2,
	[MASTER_MCU_3] = &mcu_port3,
	[MASTER_MCU_4] = &mcu_port4,
	[MASTER_GPUSYS] = &gpu,
	[MASTER_MMSYS] = &mmsys,
	[MASTER_MM_VPU] = &mm_vpu,
	[MASTER_MM_DISP] = &mm_disp,
	[MASTER_MM_VDEC] = &mm_vdec,
	[MASTER_MM_VENC] = &mm_venc,
	[MASTER_MM_CAM] = &mm_cam,
	[MASTER_MM_IMG] = &mm_img,
	[MASTER_MM_MDP] = &mm_mdp,
	[MASTER_VPUSYS] = &vpusys,
	[MASTER_VPU_0] = &vpu_port0,
	[MASTER_VPU_1] = &vpu_port1,
	[MASTER_MDLASYS] = &mdlasys,
	[MASTER_MDLA_0] = &mdla_port0,
	[MASTER_UFS] = &ufs,
	[MASTER_PCIE] = &pcie,
	[MASTER_USB] = &usb,
	[MASTER_WIFI] = &wifi,
	[MASTER_BT] = &bt,
	[MASTER_NETSYS] = &netsys,
	[MASTER_DBGIF] = &dbgif,
	[SLAVE_HRT_DDR_EMI] = &hrt_ddr_emi,
	[MASTER_HRT_MMSYS] = &hrt_mmsys,
	[MASTER_HRT_MM_DISP] = &hrt_mm_disp,
	[MASTER_HRT_MM_VDEC] = &hrt_mm_vdec,
	[MASTER_HRT_MM_VENC] = &hrt_mm_venc,
	[MASTER_HRT_MM_CAM] = &hrt_mm_cam,
	[MASTER_HRT_MM_IMG] = &hrt_mm_img,
	[MASTER_HRT_MM_MDP] = &hrt_mm_mdp,
	[MASTER_HRT_ADSP] = &hrt_adsp,
	[MASTER_HRT_DBGIF] = &hrt_dbgif
};

static struct mtk_icc_desc mt8196_emi_icc = {
	.nodes = mt8196_emi_icc_nodes,
	.num_nodes = ARRAY_SIZE(mt8196_emi_icc_nodes),
};

static const struct of_device_id mtk_mt8196_emi_icc_of_match[] = {
	{ .compatible = "mediatek,mt8196-emi", .data = &mt8196_emi_icc },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mtk_mt8196_emi_icc_of_match);

static struct platform_driver mtk_emi_icc_mt8196_driver = {
	.driver = {
		.name = "emi-icc-mt8196",
		.of_match_table = mtk_mt8196_emi_icc_of_match,
		.sync_state = icc_sync_state,
	},
	.probe = mtk_emi_icc_probe,
	.remove = mtk_emi_icc_remove,

};
module_platform_driver(mtk_emi_icc_mt8196_driver);

MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_DESCRIPTION("MediaTek MT8196 EMI ICC driver");
MODULE_LICENSE("GPL");
