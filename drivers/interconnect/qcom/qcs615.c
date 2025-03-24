// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <dt-bindings/interconnect/qcom,qcs615-rpmh.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"
#include "qcs615.h"

static struct qcom_icc_node qhm_a1noc_cfg = {
	.name = "qhm_a1noc_cfg",
	.id = QCS615_MASTER_A1NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QCS615_SLAVE_SERVICE_A2NOC },
};

static struct qcom_icc_node qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = QCS615_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QCS615_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node qhm_qspi = {
	.name = "qhm_qspi",
	.id = QCS615_MASTER_QSPI,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QCS615_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node qhm_qup0 = {
	.name = "qhm_qup0",
	.id = QCS615_MASTER_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QCS615_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node qhm_qup1 = {
	.name = "qhm_qup1",
	.id = QCS615_MASTER_BLSP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QCS615_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node qnm_cnoc = {
	.name = "qnm_cnoc",
	.id = QCS615_MASTER_CNOC_A2NOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node qxm_crypto = {
	.name = "qxm_crypto",
	.id = QCS615_MASTER_CRYPTO,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node qxm_ipa = {
	.name = "qxm_ipa",
	.id = QCS615_MASTER_IPA,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_SLAVE_LPASS_SNOC },
};

static struct qcom_icc_node xm_emac_avb = {
	.name = "xm_emac_avb",
	.id = QCS615_MASTER_EMAC_EVB,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node xm_pcie = {
	.name = "xm_pcie",
	.id = QCS615_MASTER_PCIE,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_SLAVE_ANOC_PCIE_SNOC },
};

static struct qcom_icc_node xm_qdss_etr = {
	.name = "xm_qdss_etr",
	.id = QCS615_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node xm_sdc1 = {
	.name = "xm_sdc1",
	.id = QCS615_MASTER_SDCC_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node xm_sdc2 = {
	.name = "xm_sdc2",
	.id = QCS615_MASTER_SDCC_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.id = QCS615_MASTER_UFS_MEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node xm_usb2 = {
	.name = "xm_usb2",
	.id = QCS615_MASTER_USB2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node xm_usb3_0 = {
	.name = "xm_usb3_0",
	.id = QCS615_MASTER_USB3_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node qxm_camnoc_hf0_uncomp = {
	.name = "qxm_camnoc_hf0_uncomp",
	.id = QCS615_MASTER_CAMNOC_HF0_UNCOMP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { QCS615_SLAVE_CAMNOC_UNCOMP },
};

static struct qcom_icc_node qxm_camnoc_hf1_uncomp = {
	.name = "qxm_camnoc_hf1_uncomp",
	.id = QCS615_MASTER_CAMNOC_HF1_UNCOMP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { QCS615_SLAVE_CAMNOC_UNCOMP },
};

static struct qcom_icc_node qxm_camnoc_sf_uncomp = {
	.name = "qxm_camnoc_sf_uncomp",
	.id = QCS615_MASTER_CAMNOC_SF_UNCOMP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { QCS615_SLAVE_CAMNOC_UNCOMP },
};

static struct qcom_icc_node qhm_spdm = {
	.name = "qhm_spdm",
	.id = QCS615_MASTER_SPDM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QCS615_SLAVE_CNOC_A2NOC },
};

static struct qcom_icc_node qnm_snoc = {
	.name = "qnm_snoc",
	.id = QCS615_MASTER_SNOC_CNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 39,
	.links = { QCS615_SLAVE_A1NOC_CFG, QCS615_SLAVE_AHB2PHY_EAST,
		   QCS615_SLAVE_AHB2PHY_WEST, QCS615_SLAVE_AOP,
		   QCS615_SLAVE_AOSS, QCS615_SLAVE_CAMERA_CFG,
		   QCS615_SLAVE_CLK_CTL, QCS615_SLAVE_RBCPR_CX_CFG,
		   QCS615_SLAVE_RBCPR_MX_CFG, QCS615_SLAVE_CRYPTO_0_CFG,
		   QCS615_SLAVE_CNOC_DDRSS, QCS615_SLAVE_DISPLAY_CFG,
		   QCS615_SLAVE_EMAC_AVB_CFG, QCS615_SLAVE_GLM,
		   QCS615_SLAVE_GFX3D_CFG, QCS615_SLAVE_IMEM_CFG,
		   QCS615_SLAVE_IPA_CFG, QCS615_SLAVE_CNOC_MNOC_CFG,
		   QCS615_SLAVE_PCIE_CFG, QCS615_SLAVE_PIMEM_CFG,
		   QCS615_SLAVE_PRNG, QCS615_SLAVE_QDSS_CFG,
		   QCS615_SLAVE_QSPI, QCS615_SLAVE_QUP_0,
		   QCS615_SLAVE_QUP_1, QCS615_SLAVE_SDCC_1,
		   QCS615_SLAVE_SDCC_2, QCS615_SLAVE_SNOC_CFG,
		   QCS615_SLAVE_SPDM_WRAPPER, QCS615_SLAVE_TCSR,
		   QCS615_SLAVE_TLMM_EAST, QCS615_SLAVE_TLMM_SOUTH,
		   QCS615_SLAVE_TLMM_WEST, QCS615_SLAVE_UFS_MEM_CFG,
		   QCS615_SLAVE_USB2, QCS615_SLAVE_USB3,
		   QCS615_SLAVE_VENUS_CFG, QCS615_SLAVE_VSENSE_CTRL_CFG,
		   QCS615_SLAVE_SERVICE_CNOC },
};

static struct qcom_icc_node xm_qdss_dap = {
	.name = "xm_qdss_dap",
	.id = QCS615_MASTER_QDSS_DAP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 40,
	.links = { QCS615_SLAVE_A1NOC_CFG, QCS615_SLAVE_AHB2PHY_EAST,
		   QCS615_SLAVE_AHB2PHY_WEST, QCS615_SLAVE_AOP,
		   QCS615_SLAVE_AOSS, QCS615_SLAVE_CAMERA_CFG,
		   QCS615_SLAVE_CLK_CTL, QCS615_SLAVE_RBCPR_CX_CFG,
		   QCS615_SLAVE_RBCPR_MX_CFG, QCS615_SLAVE_CRYPTO_0_CFG,
		   QCS615_SLAVE_CNOC_DDRSS, QCS615_SLAVE_DISPLAY_CFG,
		   QCS615_SLAVE_EMAC_AVB_CFG, QCS615_SLAVE_GLM,
		   QCS615_SLAVE_GFX3D_CFG, QCS615_SLAVE_IMEM_CFG,
		   QCS615_SLAVE_IPA_CFG, QCS615_SLAVE_CNOC_MNOC_CFG,
		   QCS615_SLAVE_PCIE_CFG, QCS615_SLAVE_PIMEM_CFG,
		   QCS615_SLAVE_PRNG, QCS615_SLAVE_QDSS_CFG,
		   QCS615_SLAVE_QSPI, QCS615_SLAVE_QUP_0,
		   QCS615_SLAVE_QUP_1, QCS615_SLAVE_SDCC_1,
		   QCS615_SLAVE_SDCC_2, QCS615_SLAVE_SNOC_CFG,
		   QCS615_SLAVE_SPDM_WRAPPER, QCS615_SLAVE_TCSR,
		   QCS615_SLAVE_TLMM_EAST, QCS615_SLAVE_TLMM_SOUTH,
		   QCS615_SLAVE_TLMM_WEST, QCS615_SLAVE_UFS_MEM_CFG,
		   QCS615_SLAVE_USB2, QCS615_SLAVE_USB3,
		   QCS615_SLAVE_VENUS_CFG, QCS615_SLAVE_VSENSE_CTRL_CFG,
		   QCS615_SLAVE_CNOC_A2NOC, QCS615_SLAVE_SERVICE_CNOC },
};

static struct qcom_icc_node qhm_cnoc = {
	.name = "qhm_cnoc",
	.id = QCS615_MASTER_CNOC_DC_NOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.links = { QCS615_SLAVE_DC_NOC_GEMNOC, QCS615_SLAVE_LLCC_CFG },
};

static struct qcom_icc_node acm_apps = {
	.name = "acm_apps",
	.id = QCS615_MASTER_APPSS_PROC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { QCS615_SLAVE_GEM_NOC_SNOC, QCS615_SLAVE_LLCC,
		   QCS615_SLAVE_MEM_NOC_PCIE_SNOC },
};

static struct qcom_icc_node acm_gpu_tcu = {
	.name = "acm_gpu_tcu",
	.id = QCS615_MASTER_GPU_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { QCS615_SLAVE_GEM_NOC_SNOC, QCS615_SLAVE_LLCC },
};

static struct qcom_icc_node acm_sys_tcu = {
	.name = "acm_sys_tcu",
	.id = QCS615_MASTER_SYS_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { QCS615_SLAVE_GEM_NOC_SNOC, QCS615_SLAVE_LLCC },
};

static struct qcom_icc_node qhm_gemnoc_cfg = {
	.name = "qhm_gemnoc_cfg",
	.id = QCS615_MASTER_GEM_NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.links = { QCS615_SLAVE_MSS_PROC_MS_MPU_CFG, QCS615_SLAVE_SERVICE_GEM_NOC },
};

static struct qcom_icc_node qnm_gpu = {
	.name = "qnm_gpu",
	.id = QCS615_MASTER_GFX3D,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { QCS615_SLAVE_GEM_NOC_SNOC, QCS615_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_mnoc_hf = {
	.name = "qnm_mnoc_hf",
	.id = QCS615_MASTER_MNOC_HF_MEM_NOC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { QCS615_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_mnoc_sf = {
	.name = "qnm_mnoc_sf",
	.id = QCS615_MASTER_MNOC_SF_MEM_NOC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 2,
	.links = { QCS615_SLAVE_GEM_NOC_SNOC, QCS615_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_snoc_gc = {
	.name = "qnm_snoc_gc",
	.id = QCS615_MASTER_SNOC_GC_MEM_NOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_snoc_sf = {
	.name = "qnm_snoc_sf",
	.id = QCS615_MASTER_SNOC_SF_MEM_NOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { QCS615_SLAVE_LLCC },
};

static struct qcom_icc_node ipa_core_master = {
	.name = "ipa_core_master",
	.id = QCS615_MASTER_IPA_CORE,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_SLAVE_IPA_CORE },
};

static struct qcom_icc_node llcc_mc = {
	.name = "llcc_mc",
	.id = QCS615_MASTER_LLCC,
	.channels = 2,
	.buswidth = 4,
	.num_links = 1,
	.links = { QCS615_SLAVE_EBI1 },
};

static struct qcom_icc_node qhm_mnoc_cfg = {
	.name = "qhm_mnoc_cfg",
	.id = QCS615_MASTER_CNOC_MNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QCS615_SLAVE_SERVICE_MNOC },
};

static struct qcom_icc_node qxm_camnoc_hf0 = {
	.name = "qxm_camnoc_hf0",
	.id = QCS615_MASTER_CAMNOC_HF0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { QCS615_SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qxm_camnoc_hf1 = {
	.name = "qxm_camnoc_hf1",
	.id = QCS615_MASTER_CAMNOC_HF1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { QCS615_SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qxm_camnoc_sf = {
	.name = "qxm_camnoc_sf",
	.id = QCS615_MASTER_CAMNOC_SF,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { QCS615_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qxm_mdp0 = {
	.name = "qxm_mdp0",
	.id = QCS615_MASTER_MDP0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { QCS615_SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qxm_rot = {
	.name = "qxm_rot",
	.id = QCS615_MASTER_ROTATOR,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { QCS615_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qxm_venus0 = {
	.name = "qxm_venus0",
	.id = QCS615_MASTER_VIDEO_P0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { QCS615_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qxm_venus_arm9 = {
	.name = "qxm_venus_arm9",
	.id = QCS615_MASTER_VIDEO_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qhm_snoc_cfg = {
	.name = "qhm_snoc_cfg",
	.id = QCS615_MASTER_SNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QCS615_SLAVE_SERVICE_SNOC },
};

static struct qcom_icc_node qnm_aggre1_noc = {
	.name = "qnm_aggre1_noc",
	.id = QCS615_MASTER_A1NOC_SNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 8,
	.links = { QCS615_SLAVE_APPSS, QCS615_SLAVE_SNOC_CNOC,
		   QCS615_SLAVE_SNOC_GEM_NOC_SF, QCS615_SLAVE_IMEM,
		   QCS615_SLAVE_PIMEM, QCS615_SLAVE_PCIE_0,
		   QCS615_SLAVE_QDSS_STM, QCS615_SLAVE_TCU },
};

static struct qcom_icc_node qnm_gemnoc = {
	.name = "qnm_gemnoc",
	.id = QCS615_MASTER_GEM_NOC_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 6,
	.links = { QCS615_SLAVE_APPSS, QCS615_SLAVE_SNOC_CNOC,
		   QCS615_SLAVE_IMEM, QCS615_SLAVE_PIMEM,
		   QCS615_SLAVE_QDSS_STM, QCS615_SLAVE_TCU },
};

static struct qcom_icc_node qnm_gemnoc_pcie = {
	.name = "qnm_gemnoc_pcie",
	.id = QCS615_MASTER_GEM_NOC_PCIE_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_SLAVE_PCIE_0 },
};

static struct qcom_icc_node qnm_lpass_anoc = {
	.name = "qnm_lpass_anoc",
	.id = QCS615_MASTER_LPASS_ANOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 7,
	.links = { QCS615_SLAVE_APPSS, QCS615_SLAVE_SNOC_CNOC,
		   QCS615_SLAVE_SNOC_GEM_NOC_SF, QCS615_SLAVE_IMEM,
		   QCS615_SLAVE_PIMEM, QCS615_SLAVE_PCIE_0,
		   QCS615_SLAVE_QDSS_STM },
};

static struct qcom_icc_node qnm_pcie_anoc = {
	.name = "qnm_pcie_anoc",
	.id = QCS615_MASTER_ANOC_PCIE_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 5,
	.links = { QCS615_SLAVE_APPSS, QCS615_SLAVE_SNOC_CNOC,
		   QCS615_SLAVE_SNOC_GEM_NOC_SF, QCS615_SLAVE_IMEM,
		   QCS615_SLAVE_QDSS_STM },
};

static struct qcom_icc_node qxm_pimem = {
	.name = "qxm_pimem",
	.id = QCS615_MASTER_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { QCS615_SLAVE_SNOC_MEM_NOC_GC, QCS615_SLAVE_IMEM },
};

static struct qcom_icc_node xm_gic = {
	.name = "xm_gic",
	.id = QCS615_MASTER_GIC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { QCS615_SLAVE_SNOC_MEM_NOC_GC, QCS615_SLAVE_IMEM },
};

static struct qcom_icc_node qns_a1noc_snoc = {
	.name = "qns_a1noc_snoc",
	.id = QCS615_SLAVE_A1NOC_SNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { QCS615_MASTER_A1NOC_SNOC },
};

static struct qcom_icc_node qns_lpass_snoc = {
	.name = "qns_lpass_snoc",
	.id = QCS615_SLAVE_LPASS_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_MASTER_LPASS_ANOC },
};

static struct qcom_icc_node qns_pcie_snoc = {
	.name = "qns_pcie_snoc",
	.id = QCS615_SLAVE_ANOC_PCIE_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_MASTER_ANOC_PCIE_SNOC },
};

static struct qcom_icc_node srvc_aggre2_noc = {
	.name = "srvc_aggre2_noc",
	.id = QCS615_SLAVE_SERVICE_A2NOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qns_camnoc_uncomp = {
	.name = "qns_camnoc_uncomp",
	.id = QCS615_SLAVE_CAMNOC_UNCOMP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 0,
};

static struct qcom_icc_node qhs_a1_noc_cfg = {
	.name = "qhs_a1_noc_cfg",
	.id = QCS615_SLAVE_A1NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QCS615_MASTER_A1NOC_CFG },
};

static struct qcom_icc_node qhs_ahb2phy_east = {
	.name = "qhs_ahb2phy_east",
	.id = QCS615_SLAVE_AHB2PHY_EAST,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ahb2phy_west = {
	.name = "qhs_ahb2phy_west",
	.id = QCS615_SLAVE_AHB2PHY_WEST,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_aop = {
	.name = "qhs_aop",
	.id = QCS615_SLAVE_AOP,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_aoss = {
	.name = "qhs_aoss",
	.id = QCS615_SLAVE_AOSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_camera_cfg = {
	.name = "qhs_camera_cfg",
	.id = QCS615_SLAVE_CAMERA_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = QCS615_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = QCS615_SLAVE_RBCPR_CX_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_cpr_mx = {
	.name = "qhs_cpr_mx",
	.id = QCS615_SLAVE_RBCPR_MX_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = QCS615_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ddrss_cfg = {
	.name = "qhs_ddrss_cfg",
	.id = QCS615_SLAVE_CNOC_DDRSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QCS615_MASTER_CNOC_DC_NOC },
};

static struct qcom_icc_node qhs_display_cfg = {
	.name = "qhs_display_cfg",
	.id = QCS615_SLAVE_DISPLAY_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_emac_avb_cfg = {
	.name = "qhs_emac_avb_cfg",
	.id = QCS615_SLAVE_EMAC_AVB_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_glm = {
	.name = "qhs_glm",
	.id = QCS615_SLAVE_GLM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.id = QCS615_SLAVE_GFX3D_CFG,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = QCS615_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ipa = {
	.name = "qhs_ipa",
	.id = QCS615_SLAVE_IPA_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_mnoc_cfg = {
	.name = "qhs_mnoc_cfg",
	.id = QCS615_SLAVE_CNOC_MNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QCS615_MASTER_CNOC_MNOC_CFG },
};

static struct qcom_icc_node qhs_pcie_config = {
	.name = "qhs_pcie_config",
	.id = QCS615_SLAVE_PCIE_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = QCS615_SLAVE_PIMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_prng = {
	.name = "qhs_prng",
	.id = QCS615_SLAVE_PRNG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = QCS615_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qspi = {
	.name = "qhs_qspi",
	.id = QCS615_SLAVE_QSPI,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qup0 = {
	.name = "qhs_qup0",
	.id = QCS615_SLAVE_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qup1 = {
	.name = "qhs_qup1",
	.id = QCS615_SLAVE_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_sdc1 = {
	.name = "qhs_sdc1",
	.id = QCS615_SLAVE_SDCC_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = QCS615_SLAVE_SDCC_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_snoc_cfg = {
	.name = "qhs_snoc_cfg",
	.id = QCS615_SLAVE_SNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QCS615_MASTER_SNOC_CFG },
};

static struct qcom_icc_node qhs_spdm = {
	.name = "qhs_spdm",
	.id = QCS615_SLAVE_SPDM_WRAPPER,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = QCS615_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_tlmm_east = {
	.name = "qhs_tlmm_east",
	.id = QCS615_SLAVE_TLMM_EAST,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_tlmm_south = {
	.name = "qhs_tlmm_south",
	.id = QCS615_SLAVE_TLMM_SOUTH,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_tlmm_west = {
	.name = "qhs_tlmm_west",
	.id = QCS615_SLAVE_TLMM_WEST,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.id = QCS615_SLAVE_UFS_MEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_usb2 = {
	.name = "qhs_usb2",
	.id = QCS615_SLAVE_USB2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_usb3 = {
	.name = "qhs_usb3",
	.id = QCS615_SLAVE_USB3,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = QCS615_SLAVE_VENUS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.id = QCS615_SLAVE_VSENSE_CTRL_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qns_cnoc_a2noc = {
	.name = "qns_cnoc_a2noc",
	.id = QCS615_SLAVE_CNOC_A2NOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_MASTER_CNOC_A2NOC },
};

static struct qcom_icc_node srvc_cnoc = {
	.name = "srvc_cnoc",
	.id = QCS615_SLAVE_SERVICE_CNOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_dc_noc_gemnoc = {
	.name = "qhs_dc_noc_gemnoc",
	.id = QCS615_SLAVE_DC_NOC_GEMNOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QCS615_MASTER_GEM_NOC_CFG },
};

static struct qcom_icc_node qhs_llcc = {
	.name = "qhs_llcc",
	.id = QCS615_SLAVE_LLCC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_mdsp_ms_mpu_cfg = {
	.name = "qhs_mdsp_ms_mpu_cfg",
	.id = QCS615_SLAVE_MSS_PROC_MS_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qns_gem_noc_snoc = {
	.name = "qns_gem_noc_snoc",
	.id = QCS615_SLAVE_GEM_NOC_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_MASTER_GEM_NOC_SNOC },
};

static struct qcom_icc_node qns_llcc = {
	.name = "qns_llcc",
	.id = QCS615_SLAVE_LLCC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { QCS615_MASTER_LLCC },
};

static struct qcom_icc_node qns_sys_pcie = {
	.name = "qns_sys_pcie",
	.id = QCS615_SLAVE_MEM_NOC_PCIE_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_MASTER_GEM_NOC_PCIE_SNOC },
};

static struct qcom_icc_node srvc_gemnoc = {
	.name = "srvc_gemnoc",
	.id = QCS615_SLAVE_SERVICE_GEM_NOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node ipa_core_slave = {
	.name = "ipa_core_slave",
	.id = QCS615_SLAVE_IPA_CORE,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.id = QCS615_SLAVE_EBI1,
	.channels = 2,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qns2_mem_noc = {
	.name = "qns2_mem_noc",
	.id = QCS615_SLAVE_MNOC_SF_MEM_NOC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { QCS615_MASTER_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qns_mem_noc_hf = {
	.name = "qns_mem_noc_hf",
	.id = QCS615_SLAVE_MNOC_HF_MEM_NOC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { QCS615_MASTER_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node srvc_mnoc = {
	.name = "srvc_mnoc",
	.id = QCS615_SLAVE_SERVICE_MNOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_apss = {
	.name = "qhs_apss",
	.id = QCS615_SLAVE_APPSS,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node qns_cnoc = {
	.name = "qns_cnoc",
	.id = QCS615_SLAVE_SNOC_CNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_MASTER_SNOC_CNOC },
};

static struct qcom_icc_node qns_gemnoc_sf = {
	.name = "qns_gemnoc_sf",
	.id = QCS615_SLAVE_SNOC_GEM_NOC_SF,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { QCS615_MASTER_SNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qns_memnoc_gc = {
	.name = "qns_memnoc_gc",
	.id = QCS615_SLAVE_SNOC_MEM_NOC_GC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QCS615_MASTER_SNOC_GC_MEM_NOC },
};

static struct qcom_icc_node qxs_imem = {
	.name = "qxs_imem",
	.id = QCS615_SLAVE_IMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node qxs_pimem = {
	.name = "qxs_pimem",
	.id = QCS615_SLAVE_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node srvc_snoc = {
	.name = "srvc_snoc",
	.id = QCS615_SLAVE_SERVICE_SNOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node xs_pcie = {
	.name = "xs_pcie",
	.id = QCS615_SLAVE_PCIE_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = QCS615_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = QCS615_SLAVE_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.num_nodes = 1,
	.nodes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.keepalive = true,
	.num_nodes = 37,
	.nodes = { &qhm_spdm, &qnm_snoc,
		   &qhs_a1_noc_cfg, &qhs_aop,
		   &qhs_aoss, &qhs_camera_cfg,
		   &qhs_clk_ctl, &qhs_cpr_cx,
		   &qhs_cpr_mx, &qhs_crypto0_cfg,
		   &qhs_ddrss_cfg, &qhs_display_cfg,
		   &qhs_emac_avb_cfg, &qhs_glm,
		   &qhs_gpuss_cfg, &qhs_imem_cfg,
		   &qhs_ipa, &qhs_mnoc_cfg,
		   &qhs_pcie_config, &qhs_pimem_cfg,
		   &qhs_prng, &qhs_qdss_cfg,
		   &qhs_qup0, &qhs_qup1,
		   &qhs_snoc_cfg, &qhs_spdm,
		   &qhs_tcsr, &qhs_tlmm_east,
		   &qhs_tlmm_south, &qhs_tlmm_west,
		   &qhs_ufs_mem_cfg, &qhs_usb2,
		   &qhs_usb3, &qhs_venus_cfg,
		   &qhs_vsense_ctrl_cfg, &qns_cnoc_a2noc,
		   &srvc_cnoc },
};

static struct qcom_icc_bcm bcm_cn1 = {
	.name = "CN1",
	.num_nodes = 8,
	.nodes = { &qhm_qspi, &xm_sdc1,
		   &xm_sdc2, &qhs_ahb2phy_east,
		   &qhs_ahb2phy_west, &qhs_qspi,
		   &qhs_sdc1, &qhs_sdc2 },
};

static struct qcom_icc_bcm bcm_ip0 = {
	.name = "IP0",
	.num_nodes = 1,
	.nodes = { &ipa_core_slave },
};

static struct qcom_icc_bcm bcm_mc0 = {
	.name = "MC0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_mm0 = {
	.name = "MM0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_mem_noc_hf },
};

static struct qcom_icc_bcm bcm_mm1 = {
	.name = "MM1",
	.num_nodes = 7,
	.nodes = { &qxm_camnoc_hf0_uncomp, &qxm_camnoc_hf1_uncomp,
		   &qxm_camnoc_sf_uncomp, &qxm_camnoc_hf0,
		   &qxm_camnoc_hf1, &qxm_mdp0,
		   &qxm_rot },
};

static struct qcom_icc_bcm bcm_mm2 = {
	.name = "MM2",
	.num_nodes = 2,
	.nodes = { &qxm_camnoc_sf, &qns2_mem_noc },
};

static struct qcom_icc_bcm bcm_mm3 = {
	.name = "MM3",
	.num_nodes = 2,
	.nodes = { &qxm_venus0, &qxm_venus_arm9 },
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.keepalive = true,
	.vote_scale = 1,
	.num_nodes = 2,
	.nodes = { &qhm_qup0, &qhm_qup1 },
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_sh2 = {
	.name = "SH2",
	.num_nodes = 1,
	.nodes = { &acm_apps },
};

static struct qcom_icc_bcm bcm_sh3 = {
	.name = "SH3",
	.num_nodes = 1,
	.nodes = { &qns_gem_noc_snoc },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.num_nodes = 1,
	.nodes = { &qxs_imem },
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.num_nodes = 1,
	.nodes = { &qns_memnoc_gc },
};

static struct qcom_icc_bcm bcm_sn3 = {
	.name = "SN3",
	.num_nodes = 2,
	.nodes = { &srvc_aggre2_noc, &qns_cnoc },
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.num_nodes = 1,
	.nodes = { &qxs_pimem },
};

static struct qcom_icc_bcm bcm_sn5 = {
	.name = "SN5",
	.num_nodes = 1,
	.nodes = { &xs_qdss_stm },
};

static struct qcom_icc_bcm bcm_sn8 = {
	.name = "SN8",
	.num_nodes = 2,
	.nodes = { &qnm_gemnoc_pcie, &xs_pcie },
};

static struct qcom_icc_bcm bcm_sn9 = {
	.name = "SN9",
	.num_nodes = 1,
	.nodes = { &qnm_aggre1_noc },
};

static struct qcom_icc_bcm bcm_sn12 = {
	.name = "SN12",
	.num_nodes = 2,
	.nodes = { &qxm_pimem, &xm_gic },
};

static struct qcom_icc_bcm bcm_sn13 = {
	.name = "SN13",
	.num_nodes = 1,
	.nodes = { &qnm_lpass_anoc },
};

static struct qcom_icc_bcm bcm_sn14 = {
	.name = "SN14",
	.num_nodes = 1,
	.nodes = { &qns_pcie_snoc },
};

static struct qcom_icc_bcm bcm_sn15 = {
	.name = "SN15",
	.num_nodes = 1,
	.nodes = { &qnm_gemnoc },
};

static struct qcom_icc_bcm * const aggre1_noc_bcms[] = {
	&bcm_ce0,
	&bcm_cn1,
	&bcm_qup0,
	&bcm_sn3,
	&bcm_sn14,
	&bcm_ip0,
};

static struct qcom_icc_node * const aggre1_noc_nodes[] = {
	[MASTER_A1NOC_CFG] = &qhm_a1noc_cfg,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QSPI] = &qhm_qspi,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_BLSP_1] = &qhm_qup1,
	[MASTER_CNOC_A2NOC] = &qnm_cnoc,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_EMAC_EVB] = &xm_emac_avb,
	[MASTER_PCIE] = &xm_pcie,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[MASTER_SDCC_1] = &xm_sdc1,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_USB2] = &xm_usb2,
	[MASTER_USB3_0] = &xm_usb3_0,
	[SLAVE_A1NOC_SNOC] = &qns_a1noc_snoc,
	[SLAVE_LPASS_SNOC] = &qns_lpass_snoc,
	[SLAVE_ANOC_PCIE_SNOC] = &qns_pcie_snoc,
	[SLAVE_SERVICE_A2NOC] = &srvc_aggre2_noc,
};

static const struct qcom_icc_desc qcs615_aggre1_noc = {
	.nodes = aggre1_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre1_noc_nodes),
	.bcms = aggre1_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_noc_bcms),
};

static struct qcom_icc_bcm * const camnoc_virt_bcms[] = {
	&bcm_mm1,
};

static struct qcom_icc_node * const camnoc_virt_nodes[] = {
	[MASTER_CAMNOC_HF0_UNCOMP] = &qxm_camnoc_hf0_uncomp,
	[MASTER_CAMNOC_HF1_UNCOMP] = &qxm_camnoc_hf1_uncomp,
	[MASTER_CAMNOC_SF_UNCOMP] = &qxm_camnoc_sf_uncomp,
	[SLAVE_CAMNOC_UNCOMP] = &qns_camnoc_uncomp,
};

static const struct qcom_icc_desc qcs615_camnoc_virt = {
	.nodes = camnoc_virt_nodes,
	.num_nodes = ARRAY_SIZE(camnoc_virt_nodes),
	.bcms = camnoc_virt_bcms,
	.num_bcms = ARRAY_SIZE(camnoc_virt_bcms),
};

static struct qcom_icc_bcm * const config_noc_bcms[] = {
	&bcm_cn0,
	&bcm_cn1,
};

static struct qcom_icc_node * const config_noc_nodes[] = {
	[MASTER_SPDM] = &qhm_spdm,
	[MASTER_SNOC_CNOC] = &qnm_snoc,
	[MASTER_QDSS_DAP] = &xm_qdss_dap,
	[SLAVE_A1NOC_CFG] = &qhs_a1_noc_cfg,
	[SLAVE_AHB2PHY_EAST] = &qhs_ahb2phy_east,
	[SLAVE_AHB2PHY_WEST] = &qhs_ahb2phy_west,
	[SLAVE_AOP] = &qhs_aop,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MX_CFG] = &qhs_cpr_mx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_CNOC_DDRSS] = &qhs_ddrss_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_EMAC_AVB_CFG] = &qhs_emac_avb_cfg,
	[SLAVE_GLM] = &qhs_glm,
	[SLAVE_GFX3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_CNOC_MNOC_CFG] = &qhs_mnoc_cfg,
	[SLAVE_PCIE_CFG] = &qhs_pcie_config,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QSPI] = &qhs_qspi,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_SDCC_1] = &qhs_sdc1,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SNOC_CFG] = &qhs_snoc_cfg,
	[SLAVE_SPDM_WRAPPER] = &qhs_spdm,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM_EAST] = &qhs_tlmm_east,
	[SLAVE_TLMM_SOUTH] = &qhs_tlmm_south,
	[SLAVE_TLMM_WEST] = &qhs_tlmm_west,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB2] = &qhs_usb2,
	[SLAVE_USB3] = &qhs_usb3,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_CNOC_A2NOC] = &qns_cnoc_a2noc,
	[SLAVE_SERVICE_CNOC] = &srvc_cnoc,
};

static const struct qcom_icc_desc qcs615_config_noc = {
	.nodes = config_noc_nodes,
	.num_nodes = ARRAY_SIZE(config_noc_nodes),
	.bcms = config_noc_bcms,
	.num_bcms = ARRAY_SIZE(config_noc_bcms),
};

static struct qcom_icc_node * const dc_noc_nodes[] = {
	[MASTER_CNOC_DC_NOC] = &qhm_cnoc,
	[SLAVE_DC_NOC_GEMNOC] = &qhs_dc_noc_gemnoc,
	[SLAVE_LLCC_CFG] = &qhs_llcc,
};

static const struct qcom_icc_desc qcs615_dc_noc = {
	.nodes = dc_noc_nodes,
	.num_nodes = ARRAY_SIZE(dc_noc_nodes),
};

static struct qcom_icc_bcm * const gem_noc_bcms[] = {
	&bcm_sh0,
	&bcm_sh2,
	&bcm_sh3,
	&bcm_mm1,
};

static struct qcom_icc_node * const gem_noc_nodes[] = {
	[MASTER_APPSS_PROC] = &acm_apps,
	[MASTER_GPU_TCU] = &acm_gpu_tcu,
	[MASTER_SYS_TCU] = &acm_sys_tcu,
	[MASTER_GEM_NOC_CFG] = &qhm_gemnoc_cfg,
	[MASTER_GFX3D] = &qnm_gpu,
	[MASTER_MNOC_HF_MEM_NOC] = &qnm_mnoc_hf,
	[MASTER_MNOC_SF_MEM_NOC] = &qnm_mnoc_sf,
	[MASTER_SNOC_GC_MEM_NOC] = &qnm_snoc_gc,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[SLAVE_MSS_PROC_MS_MPU_CFG] = &qhs_mdsp_ms_mpu_cfg,
	[SLAVE_GEM_NOC_SNOC] = &qns_gem_noc_snoc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_MEM_NOC_PCIE_SNOC] = &qns_sys_pcie,
	[SLAVE_SERVICE_GEM_NOC] = &srvc_gemnoc,
};

static const struct qcom_icc_desc qcs615_gem_noc = {
	.nodes = gem_noc_nodes,
	.num_nodes = ARRAY_SIZE(gem_noc_nodes),
	.bcms = gem_noc_bcms,
	.num_bcms = ARRAY_SIZE(gem_noc_bcms),
};

static struct qcom_icc_bcm * const ipa_virt_bcms[] = {
	&bcm_ip0,
};

static struct qcom_icc_node * const ipa_virt_nodes[] = {
	[MASTER_IPA_CORE] = &ipa_core_master,
	[SLAVE_IPA_CORE] = &ipa_core_slave,
};

static const struct qcom_icc_desc qcs615_ipa_virt = {
	.nodes = ipa_virt_nodes,
	.num_nodes = ARRAY_SIZE(ipa_virt_nodes),
	.bcms = ipa_virt_bcms,
	.num_bcms = ARRAY_SIZE(ipa_virt_bcms),
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
};

static struct qcom_icc_node * const mc_virt_nodes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
};

static const struct qcom_icc_desc qcs615_mc_virt = {
	.nodes = mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm * const mmss_noc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
	&bcm_mm2,
	&bcm_mm3,
};

static struct qcom_icc_node * const mmss_noc_nodes[] = {
	[MASTER_CNOC_MNOC_CFG] = &qhm_mnoc_cfg,
	[MASTER_CAMNOC_HF0] = &qxm_camnoc_hf0,
	[MASTER_CAMNOC_HF1] = &qxm_camnoc_hf1,
	[MASTER_CAMNOC_SF] = &qxm_camnoc_sf,
	[MASTER_MDP0] = &qxm_mdp0,
	[MASTER_ROTATOR] = &qxm_rot,
	[MASTER_VIDEO_P0] = &qxm_venus0,
	[MASTER_VIDEO_PROC] = &qxm_venus_arm9,
	[SLAVE_MNOC_SF_MEM_NOC] = &qns2_mem_noc,
	[SLAVE_MNOC_HF_MEM_NOC] = &qns_mem_noc_hf,
	[SLAVE_SERVICE_MNOC] = &srvc_mnoc,
};

static const struct qcom_icc_desc qcs615_mmss_noc = {
	.nodes = mmss_noc_nodes,
	.num_nodes = ARRAY_SIZE(mmss_noc_nodes),
	.bcms = mmss_noc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_noc_bcms),
};

static struct qcom_icc_bcm * const system_noc_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn2,
	&bcm_sn3,
	&bcm_sn4,
	&bcm_sn5,
	&bcm_sn8,
	&bcm_sn9,
	&bcm_sn12,
	&bcm_sn13,
	&bcm_sn15,
};

static struct qcom_icc_node * const system_noc_nodes[] = {
	[MASTER_SNOC_CFG] = &qhm_snoc_cfg,
	[MASTER_A1NOC_SNOC] = &qnm_aggre1_noc,
	[MASTER_GEM_NOC_SNOC] = &qnm_gemnoc,
	[MASTER_GEM_NOC_PCIE_SNOC] = &qnm_gemnoc_pcie,
	[MASTER_LPASS_ANOC] = &qnm_lpass_anoc,
	[MASTER_ANOC_PCIE_SNOC] = &qnm_pcie_anoc,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_GIC] = &xm_gic,
	[SLAVE_APPSS] = &qhs_apss,
	[SLAVE_SNOC_CNOC] = &qns_cnoc,
	[SLAVE_SNOC_GEM_NOC_SF] = &qns_gemnoc_sf,
	[SLAVE_SNOC_MEM_NOC_GC] = &qns_memnoc_gc,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SERVICE_SNOC] = &srvc_snoc,
	[SLAVE_PCIE_0] = &xs_pcie,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc qcs615_system_noc = {
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,qcs615-aggre1-noc",
	  .data = &qcs615_aggre1_noc},
	{ .compatible = "qcom,qcs615-camnoc-virt",
	  .data = &qcs615_camnoc_virt},
	{ .compatible = "qcom,qcs615-config-noc",
	  .data = &qcs615_config_noc},
	{ .compatible = "qcom,qcs615-dc-noc",
	  .data = &qcs615_dc_noc},
	{ .compatible = "qcom,qcs615-gem-noc",
	  .data = &qcs615_gem_noc},
	{ .compatible = "qcom,qcs615-ipa-virt",
	  .data = &qcs615_ipa_virt},
	{ .compatible = "qcom,qcs615-mc-virt",
	  .data = &qcs615_mc_virt},
	{ .compatible = "qcom,qcs615-mmss-noc",
	  .data = &qcs615_mmss_noc},
	{ .compatible = "qcom,qcs615-system-noc",
	  .data = &qcs615_system_noc},
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-qcs615",
		.of_match_table = qnoc_of_match,
		.sync_state = icc_sync_state,
	},
};

static int __init qnoc_driver_init(void)
{
	return platform_driver_register(&qnoc_driver);
}
core_initcall(qnoc_driver_init);

static void __exit qnoc_driver_exit(void)
{
	platform_driver_unregister(&qnoc_driver);
}
module_exit(qnoc_driver_exit);

MODULE_DESCRIPTION("qcs615 NoC driver");
MODULE_LICENSE("GPL");
