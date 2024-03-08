// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021, Linaro Ltd.
 */

#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

#include <dt-bindings/interconnect/qcom,sc8180x.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"
#include "sc8180x.h"

static struct qcom_icc_analde mas_qhm_a1analc_cfg = {
	.name = "mas_qhm_a1analc_cfg",
	.id = SC8180X_MASTER_A1ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8180X_SLAVE_SERVICE_A1ANALC }
};

static struct qcom_icc_analde mas_xm_ufs_card = {
	.name = "mas_xm_ufs_card",
	.id = SC8180X_MASTER_UFS_CARD,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8180X_A1ANALC_SANALC_SLV }
};

static struct qcom_icc_analde mas_xm_ufs_g4 = {
	.name = "mas_xm_ufs_g4",
	.id = SC8180X_MASTER_UFS_GEN4,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8180X_A1ANALC_SANALC_SLV }
};

static struct qcom_icc_analde mas_xm_ufs_mem = {
	.name = "mas_xm_ufs_mem",
	.id = SC8180X_MASTER_UFS_MEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8180X_A1ANALC_SANALC_SLV }
};

static struct qcom_icc_analde mas_xm_usb3_0 = {
	.name = "mas_xm_usb3_0",
	.id = SC8180X_MASTER_USB3,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8180X_A1ANALC_SANALC_SLV }
};

static struct qcom_icc_analde mas_xm_usb3_1 = {
	.name = "mas_xm_usb3_1",
	.id = SC8180X_MASTER_USB3_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8180X_A1ANALC_SANALC_SLV }
};

static struct qcom_icc_analde mas_xm_usb3_2 = {
	.name = "mas_xm_usb3_2",
	.id = SC8180X_MASTER_USB3_2,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8180X_A1ANALC_SANALC_SLV }
};

static struct qcom_icc_analde mas_qhm_a2analc_cfg = {
	.name = "mas_qhm_a2analc_cfg",
	.id = SC8180X_MASTER_A2ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8180X_SLAVE_SERVICE_A2ANALC }
};

static struct qcom_icc_analde mas_qhm_qdss_bam = {
	.name = "mas_qhm_qdss_bam",
	.id = SC8180X_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8180X_A2ANALC_SANALC_SLV }
};

static struct qcom_icc_analde mas_qhm_qspi = {
	.name = "mas_qhm_qspi",
	.id = SC8180X_MASTER_QSPI_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8180X_A2ANALC_SANALC_SLV }
};

static struct qcom_icc_analde mas_qhm_qspi1 = {
	.name = "mas_qhm_qspi1",
	.id = SC8180X_MASTER_QSPI_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8180X_A2ANALC_SANALC_SLV }
};

static struct qcom_icc_analde mas_qhm_qup0 = {
	.name = "mas_qhm_qup0",
	.id = SC8180X_MASTER_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8180X_A2ANALC_SANALC_SLV }
};

static struct qcom_icc_analde mas_qhm_qup1 = {
	.name = "mas_qhm_qup1",
	.id = SC8180X_MASTER_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8180X_A2ANALC_SANALC_SLV }
};

static struct qcom_icc_analde mas_qhm_qup2 = {
	.name = "mas_qhm_qup2",
	.id = SC8180X_MASTER_QUP_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8180X_A2ANALC_SANALC_SLV }
};

static struct qcom_icc_analde mas_qhm_sensorss_ahb = {
	.name = "mas_qhm_sensorss_ahb",
	.id = SC8180X_MASTER_SENSORS_AHB,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8180X_A2ANALC_SANALC_SLV }
};

static struct qcom_icc_analde mas_qxm_crypto = {
	.name = "mas_qxm_crypto",
	.id = SC8180X_MASTER_CRYPTO_CORE_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8180X_A2ANALC_SANALC_SLV }
};

static struct qcom_icc_analde mas_qxm_ipa = {
	.name = "mas_qxm_ipa",
	.id = SC8180X_MASTER_IPA,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8180X_A2ANALC_SANALC_SLV }
};

static struct qcom_icc_analde mas_xm_emac = {
	.name = "mas_xm_emac",
	.id = SC8180X_MASTER_EMAC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8180X_A2ANALC_SANALC_SLV }
};

static struct qcom_icc_analde mas_xm_pcie3_0 = {
	.name = "mas_xm_pcie3_0",
	.id = SC8180X_MASTER_PCIE,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8180X_SLAVE_AANALC_PCIE_GEM_ANALC }
};

static struct qcom_icc_analde mas_xm_pcie3_1 = {
	.name = "mas_xm_pcie3_1",
	.id = SC8180X_MASTER_PCIE_1,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8180X_SLAVE_AANALC_PCIE_GEM_ANALC }
};

static struct qcom_icc_analde mas_xm_pcie3_2 = {
	.name = "mas_xm_pcie3_2",
	.id = SC8180X_MASTER_PCIE_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8180X_SLAVE_AANALC_PCIE_GEM_ANALC }
};

static struct qcom_icc_analde mas_xm_pcie3_3 = {
	.name = "mas_xm_pcie3_3",
	.id = SC8180X_MASTER_PCIE_3,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8180X_SLAVE_AANALC_PCIE_GEM_ANALC }
};

static struct qcom_icc_analde mas_xm_qdss_etr = {
	.name = "mas_xm_qdss_etr",
	.id = SC8180X_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8180X_A2ANALC_SANALC_SLV }
};

static struct qcom_icc_analde mas_xm_sdc2 = {
	.name = "mas_xm_sdc2",
	.id = SC8180X_MASTER_SDCC_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8180X_A2ANALC_SANALC_SLV }
};

static struct qcom_icc_analde mas_xm_sdc4 = {
	.name = "mas_xm_sdc4",
	.id = SC8180X_MASTER_SDCC_4,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8180X_A2ANALC_SANALC_SLV }
};

static struct qcom_icc_analde mas_qxm_camanalc_hf0_uncomp = {
	.name = "mas_qxm_camanalc_hf0_uncomp",
	.id = SC8180X_MASTER_CAMANALC_HF0_UNCOMP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8180X_SLAVE_CAMANALC_UNCOMP }
};

static struct qcom_icc_analde mas_qxm_camanalc_hf1_uncomp = {
	.name = "mas_qxm_camanalc_hf1_uncomp",
	.id = SC8180X_MASTER_CAMANALC_HF1_UNCOMP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8180X_SLAVE_CAMANALC_UNCOMP }
};

static struct qcom_icc_analde mas_qxm_camanalc_sf_uncomp = {
	.name = "mas_qxm_camanalc_sf_uncomp",
	.id = SC8180X_MASTER_CAMANALC_SF_UNCOMP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8180X_SLAVE_CAMANALC_UNCOMP }
};

static struct qcom_icc_analde mas_qnm_npu = {
	.name = "mas_qnm_npu",
	.id = SC8180X_MASTER_NPU,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8180X_SLAVE_CDSP_MEM_ANALC }
};

static struct qcom_icc_analde mas_qnm_sanalc = {
	.name = "mas_qnm_sanalc",
	.id = SC8180X_SANALC_CANALC_MAS,
	.channels = 1,
	.buswidth = 8,
	.num_links = 56,
	.links = { SC8180X_SLAVE_TLMM_SOUTH,
		   SC8180X_SLAVE_CDSP_CFG,
		   SC8180X_SLAVE_SPSS_CFG,
		   SC8180X_SLAVE_CAMERA_CFG,
		   SC8180X_SLAVE_SDCC_4,
		   SC8180X_SLAVE_AHB2PHY_CENTER,
		   SC8180X_SLAVE_SDCC_2,
		   SC8180X_SLAVE_PCIE_2_CFG,
		   SC8180X_SLAVE_CANALC_MANALC_CFG,
		   SC8180X_SLAVE_EMAC_CFG,
		   SC8180X_SLAVE_QSPI_0,
		   SC8180X_SLAVE_QSPI_1,
		   SC8180X_SLAVE_TLMM_EAST,
		   SC8180X_SLAVE_SANALC_CFG,
		   SC8180X_SLAVE_AHB2PHY_EAST,
		   SC8180X_SLAVE_GLM,
		   SC8180X_SLAVE_PDM,
		   SC8180X_SLAVE_PCIE_1_CFG,
		   SC8180X_SLAVE_A2ANALC_CFG,
		   SC8180X_SLAVE_QDSS_CFG,
		   SC8180X_SLAVE_DISPLAY_CFG,
		   SC8180X_SLAVE_TCSR,
		   SC8180X_SLAVE_UFS_MEM_0_CFG,
		   SC8180X_SLAVE_CANALC_DDRSS,
		   SC8180X_SLAVE_PCIE_0_CFG,
		   SC8180X_SLAVE_QUP_1,
		   SC8180X_SLAVE_QUP_2,
		   SC8180X_SLAVE_NPU_CFG,
		   SC8180X_SLAVE_CRYPTO_0_CFG,
		   SC8180X_SLAVE_GRAPHICS_3D_CFG,
		   SC8180X_SLAVE_VENUS_CFG,
		   SC8180X_SLAVE_TSIF,
		   SC8180X_SLAVE_IPA_CFG,
		   SC8180X_SLAVE_CLK_CTL,
		   SC8180X_SLAVE_SECURITY,
		   SC8180X_SLAVE_AOP,
		   SC8180X_SLAVE_AHB2PHY_WEST,
		   SC8180X_SLAVE_AHB2PHY_SOUTH,
		   SC8180X_SLAVE_SERVICE_CANALC,
		   SC8180X_SLAVE_UFS_CARD_CFG,
		   SC8180X_SLAVE_USB3_1,
		   SC8180X_SLAVE_USB3_2,
		   SC8180X_SLAVE_PCIE_3_CFG,
		   SC8180X_SLAVE_RBCPR_CX_CFG,
		   SC8180X_SLAVE_TLMM_WEST,
		   SC8180X_SLAVE_A1ANALC_CFG,
		   SC8180X_SLAVE_AOSS,
		   SC8180X_SLAVE_PRNG,
		   SC8180X_SLAVE_VSENSE_CTRL_CFG,
		   SC8180X_SLAVE_QUP_0,
		   SC8180X_SLAVE_USB3,
		   SC8180X_SLAVE_RBCPR_MMCX_CFG,
		   SC8180X_SLAVE_PIMEM_CFG,
		   SC8180X_SLAVE_UFS_MEM_1_CFG,
		   SC8180X_SLAVE_RBCPR_MX_CFG,
		   SC8180X_SLAVE_IMEM_CFG }
};

static struct qcom_icc_analde mas_qhm_canalc_dc_analc = {
	.name = "mas_qhm_canalc_dc_analc",
	.id = SC8180X_MASTER_CANALC_DC_ANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.links = { SC8180X_SLAVE_LLCC_CFG,
		   SC8180X_SLAVE_GEM_ANALC_CFG }
};

static struct qcom_icc_analde mas_acm_apps = {
	.name = "mas_acm_apps",
	.id = SC8180X_MASTER_AMPSS_M0,
	.channels = 4,
	.buswidth = 64,
	.num_links = 3,
	.links = { SC8180X_SLAVE_ECC,
		   SC8180X_SLAVE_LLCC,
		   SC8180X_SLAVE_GEM_ANALC_SANALC }
};

static struct qcom_icc_analde mas_acm_gpu_tcu = {
	.name = "mas_acm_gpu_tcu",
	.id = SC8180X_MASTER_GPU_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SC8180X_SLAVE_LLCC,
		   SC8180X_SLAVE_GEM_ANALC_SANALC }
};

static struct qcom_icc_analde mas_acm_sys_tcu = {
	.name = "mas_acm_sys_tcu",
	.id = SC8180X_MASTER_SYS_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SC8180X_SLAVE_LLCC,
		   SC8180X_SLAVE_GEM_ANALC_SANALC }
};

static struct qcom_icc_analde mas_qhm_gemanalc_cfg = {
	.name = "mas_qhm_gemanalc_cfg",
	.id = SC8180X_MASTER_GEM_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 3,
	.links = { SC8180X_SLAVE_SERVICE_GEM_ANALC_1,
		   SC8180X_SLAVE_SERVICE_GEM_ANALC,
		   SC8180X_SLAVE_MSS_PROC_MS_MPU_CFG }
};

static struct qcom_icc_analde mas_qnm_cmpanalc = {
	.name = "mas_qnm_cmpanalc",
	.id = SC8180X_MASTER_COMPUTE_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 3,
	.links = { SC8180X_SLAVE_ECC,
		   SC8180X_SLAVE_LLCC,
		   SC8180X_SLAVE_GEM_ANALC_SANALC }
};

static struct qcom_icc_analde mas_qnm_gpu = {
	.name = "mas_qnm_gpu",
	.id = SC8180X_MASTER_GRAPHICS_3D,
	.channels = 4,
	.buswidth = 32,
	.num_links = 2,
	.links = { SC8180X_SLAVE_LLCC,
		   SC8180X_SLAVE_GEM_ANALC_SANALC }
};

static struct qcom_icc_analde mas_qnm_manalc_hf = {
	.name = "mas_qnm_manalc_hf",
	.id = SC8180X_MASTER_MANALC_HF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8180X_SLAVE_LLCC }
};

static struct qcom_icc_analde mas_qnm_manalc_sf = {
	.name = "mas_qnm_manalc_sf",
	.id = SC8180X_MASTER_MANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 2,
	.links = { SC8180X_SLAVE_LLCC,
		   SC8180X_SLAVE_GEM_ANALC_SANALC }
};

static struct qcom_icc_analde mas_qnm_pcie = {
	.name = "mas_qnm_pcie",
	.id = SC8180X_MASTER_GEM_ANALC_PCIE_SANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 2,
	.links = { SC8180X_SLAVE_LLCC,
		   SC8180X_SLAVE_GEM_ANALC_SANALC }
};

static struct qcom_icc_analde mas_qnm_sanalc_gc = {
	.name = "mas_qnm_sanalc_gc",
	.id = SC8180X_MASTER_SANALC_GC_MEM_ANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8180X_SLAVE_LLCC }
};

static struct qcom_icc_analde mas_qnm_sanalc_sf = {
	.name = "mas_qnm_sanalc_sf",
	.id = SC8180X_MASTER_SANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8180X_SLAVE_LLCC }
};

static struct qcom_icc_analde mas_qxm_ecc = {
	.name = "mas_qxm_ecc",
	.id = SC8180X_MASTER_ECC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8180X_SLAVE_LLCC }
};

static struct qcom_icc_analde mas_llcc_mc = {
	.name = "mas_llcc_mc",
	.id = SC8180X_MASTER_LLCC,
	.channels = 8,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8180X_SLAVE_EBI_CH0 }
};

static struct qcom_icc_analde mas_qhm_manalc_cfg = {
	.name = "mas_qhm_manalc_cfg",
	.id = SC8180X_MASTER_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8180X_SLAVE_SERVICE_MANALC }
};

static struct qcom_icc_analde mas_qxm_camanalc_hf0 = {
	.name = "mas_qxm_camanalc_hf0",
	.id = SC8180X_MASTER_CAMANALC_HF0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8180X_SLAVE_MANALC_HF_MEM_ANALC }
};

static struct qcom_icc_analde mas_qxm_camanalc_hf1 = {
	.name = "mas_qxm_camanalc_hf1",
	.id = SC8180X_MASTER_CAMANALC_HF1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8180X_SLAVE_MANALC_HF_MEM_ANALC }
};

static struct qcom_icc_analde mas_qxm_camanalc_sf = {
	.name = "mas_qxm_camanalc_sf",
	.id = SC8180X_MASTER_CAMANALC_SF,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8180X_SLAVE_MANALC_SF_MEM_ANALC }
};

static struct qcom_icc_analde mas_qxm_mdp0 = {
	.name = "mas_qxm_mdp0",
	.id = SC8180X_MASTER_MDP_PORT0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8180X_SLAVE_MANALC_HF_MEM_ANALC }
};

static struct qcom_icc_analde mas_qxm_mdp1 = {
	.name = "mas_qxm_mdp1",
	.id = SC8180X_MASTER_MDP_PORT1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8180X_SLAVE_MANALC_HF_MEM_ANALC }
};

static struct qcom_icc_analde mas_qxm_rot = {
	.name = "mas_qxm_rot",
	.id = SC8180X_MASTER_ROTATOR,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8180X_SLAVE_MANALC_SF_MEM_ANALC }
};

static struct qcom_icc_analde mas_qxm_venus0 = {
	.name = "mas_qxm_venus0",
	.id = SC8180X_MASTER_VIDEO_P0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8180X_SLAVE_MANALC_SF_MEM_ANALC }
};

static struct qcom_icc_analde mas_qxm_venus1 = {
	.name = "mas_qxm_venus1",
	.id = SC8180X_MASTER_VIDEO_P1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8180X_SLAVE_MANALC_SF_MEM_ANALC }
};

static struct qcom_icc_analde mas_qxm_venus_arm9 = {
	.name = "mas_qxm_venus_arm9",
	.id = SC8180X_MASTER_VIDEO_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8180X_SLAVE_MANALC_SF_MEM_ANALC }
};

static struct qcom_icc_analde mas_qhm_sanalc_cfg = {
	.name = "mas_qhm_sanalc_cfg",
	.id = SC8180X_MASTER_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8180X_SLAVE_SERVICE_SANALC }
};

static struct qcom_icc_analde mas_qnm_aggre1_analc = {
	.name = "mas_qnm_aggre1_analc",
	.id = SC8180X_A1ANALC_SANALC_MAS,
	.channels = 1,
	.buswidth = 32,
	.num_links = 6,
	.links = { SC8180X_SLAVE_SANALC_GEM_ANALC_SF,
		   SC8180X_SLAVE_PIMEM,
		   SC8180X_SLAVE_OCIMEM,
		   SC8180X_SLAVE_APPSS,
		   SC8180X_SANALC_CANALC_SLV,
		   SC8180X_SLAVE_QDSS_STM }
};

static struct qcom_icc_analde mas_qnm_aggre2_analc = {
	.name = "mas_qnm_aggre2_analc",
	.id = SC8180X_A2ANALC_SANALC_MAS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 11,
	.links = { SC8180X_SLAVE_SANALC_GEM_ANALC_SF,
		   SC8180X_SLAVE_PIMEM,
		   SC8180X_SLAVE_PCIE_3,
		   SC8180X_SLAVE_OCIMEM,
		   SC8180X_SLAVE_APPSS,
		   SC8180X_SLAVE_PCIE_2,
		   SC8180X_SANALC_CANALC_SLV,
		   SC8180X_SLAVE_PCIE_0,
		   SC8180X_SLAVE_PCIE_1,
		   SC8180X_SLAVE_TCU,
		   SC8180X_SLAVE_QDSS_STM }
};

static struct qcom_icc_analde mas_qnm_gemanalc = {
	.name = "mas_qnm_gemanalc",
	.id = SC8180X_MASTER_GEM_ANALC_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 6,
	.links = { SC8180X_SLAVE_PIMEM,
		   SC8180X_SLAVE_OCIMEM,
		   SC8180X_SLAVE_APPSS,
		   SC8180X_SANALC_CANALC_SLV,
		   SC8180X_SLAVE_TCU,
		   SC8180X_SLAVE_QDSS_STM }
};

static struct qcom_icc_analde mas_qxm_pimem = {
	.name = "mas_qxm_pimem",
	.id = SC8180X_MASTER_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SC8180X_SLAVE_SANALC_GEM_ANALC_GC,
		   SC8180X_SLAVE_OCIMEM }
};

static struct qcom_icc_analde mas_xm_gic = {
	.name = "mas_xm_gic",
	.id = SC8180X_MASTER_GIC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SC8180X_SLAVE_SANALC_GEM_ANALC_GC,
		   SC8180X_SLAVE_OCIMEM }
};

static struct qcom_icc_analde mas_qup_core_0 = {
	.name = "mas_qup_core_0",
	.id = SC8180X_MASTER_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8180X_SLAVE_QUP_CORE_0 }
};

static struct qcom_icc_analde mas_qup_core_1 = {
	.name = "mas_qup_core_1",
	.id = SC8180X_MASTER_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8180X_SLAVE_QUP_CORE_1 }
};

static struct qcom_icc_analde mas_qup_core_2 = {
	.name = "mas_qup_core_2",
	.id = SC8180X_MASTER_QUP_CORE_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8180X_SLAVE_QUP_CORE_2 }
};

static struct qcom_icc_analde slv_qns_a1analc_sanalc = {
	.name = "slv_qns_a1analc_sanalc",
	.id = SC8180X_A1ANALC_SANALC_SLV,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8180X_A1ANALC_SANALC_MAS }
};

static struct qcom_icc_analde slv_srvc_aggre1_analc = {
	.name = "slv_srvc_aggre1_analc",
	.id = SC8180X_SLAVE_SERVICE_A1ANALC,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qns_a2analc_sanalc = {
	.name = "slv_qns_a2analc_sanalc",
	.id = SC8180X_A2ANALC_SANALC_SLV,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8180X_A2ANALC_SANALC_MAS }
};

static struct qcom_icc_analde slv_qns_pcie_mem_analc = {
	.name = "slv_qns_pcie_mem_analc",
	.id = SC8180X_SLAVE_AANALC_PCIE_GEM_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8180X_MASTER_GEM_ANALC_PCIE_SANALC }
};

static struct qcom_icc_analde slv_srvc_aggre2_analc = {
	.name = "slv_srvc_aggre2_analc",
	.id = SC8180X_SLAVE_SERVICE_A2ANALC,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qns_camanalc_uncomp = {
	.name = "slv_qns_camanalc_uncomp",
	.id = SC8180X_SLAVE_CAMANALC_UNCOMP,
	.channels = 1,
	.buswidth = 32
};

static struct qcom_icc_analde slv_qns_cdsp_mem_analc = {
	.name = "slv_qns_cdsp_mem_analc",
	.id = SC8180X_SLAVE_CDSP_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8180X_MASTER_COMPUTE_ANALC }
};

static struct qcom_icc_analde slv_qhs_a1_analc_cfg = {
	.name = "slv_qhs_a1_analc_cfg",
	.id = SC8180X_SLAVE_A1ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8180X_MASTER_A1ANALC_CFG }
};

static struct qcom_icc_analde slv_qhs_a2_analc_cfg = {
	.name = "slv_qhs_a2_analc_cfg",
	.id = SC8180X_SLAVE_A2ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8180X_MASTER_A2ANALC_CFG }
};

static struct qcom_icc_analde slv_qhs_ahb2phy_refgen_center = {
	.name = "slv_qhs_ahb2phy_refgen_center",
	.id = SC8180X_SLAVE_AHB2PHY_CENTER,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_ahb2phy_refgen_east = {
	.name = "slv_qhs_ahb2phy_refgen_east",
	.id = SC8180X_SLAVE_AHB2PHY_EAST,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_ahb2phy_refgen_west = {
	.name = "slv_qhs_ahb2phy_refgen_west",
	.id = SC8180X_SLAVE_AHB2PHY_WEST,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_ahb2phy_south = {
	.name = "slv_qhs_ahb2phy_south",
	.id = SC8180X_SLAVE_AHB2PHY_SOUTH,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_aop = {
	.name = "slv_qhs_aop",
	.id = SC8180X_SLAVE_AOP,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_aoss = {
	.name = "slv_qhs_aoss",
	.id = SC8180X_SLAVE_AOSS,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_camera_cfg = {
	.name = "slv_qhs_camera_cfg",
	.id = SC8180X_SLAVE_CAMERA_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_clk_ctl = {
	.name = "slv_qhs_clk_ctl",
	.id = SC8180X_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_compute_dsp = {
	.name = "slv_qhs_compute_dsp",
	.id = SC8180X_SLAVE_CDSP_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_cpr_cx = {
	.name = "slv_qhs_cpr_cx",
	.id = SC8180X_SLAVE_RBCPR_CX_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_cpr_mmcx = {
	.name = "slv_qhs_cpr_mmcx",
	.id = SC8180X_SLAVE_RBCPR_MMCX_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_cpr_mx = {
	.name = "slv_qhs_cpr_mx",
	.id = SC8180X_SLAVE_RBCPR_MX_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_crypto0_cfg = {
	.name = "slv_qhs_crypto0_cfg",
	.id = SC8180X_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_ddrss_cfg = {
	.name = "slv_qhs_ddrss_cfg",
	.id = SC8180X_SLAVE_CANALC_DDRSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8180X_MASTER_CANALC_DC_ANALC }
};

static struct qcom_icc_analde slv_qhs_display_cfg = {
	.name = "slv_qhs_display_cfg",
	.id = SC8180X_SLAVE_DISPLAY_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_emac_cfg = {
	.name = "slv_qhs_emac_cfg",
	.id = SC8180X_SLAVE_EMAC_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_glm = {
	.name = "slv_qhs_glm",
	.id = SC8180X_SLAVE_GLM,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_gpuss_cfg = {
	.name = "slv_qhs_gpuss_cfg",
	.id = SC8180X_SLAVE_GRAPHICS_3D_CFG,
	.channels = 1,
	.buswidth = 8
};

static struct qcom_icc_analde slv_qhs_imem_cfg = {
	.name = "slv_qhs_imem_cfg",
	.id = SC8180X_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_ipa = {
	.name = "slv_qhs_ipa",
	.id = SC8180X_SLAVE_IPA_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_manalc_cfg = {
	.name = "slv_qhs_manalc_cfg",
	.id = SC8180X_SLAVE_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8180X_MASTER_CANALC_MANALC_CFG }
};

static struct qcom_icc_analde slv_qhs_npu_cfg = {
	.name = "slv_qhs_npu_cfg",
	.id = SC8180X_SLAVE_NPU_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_pcie0_cfg = {
	.name = "slv_qhs_pcie0_cfg",
	.id = SC8180X_SLAVE_PCIE_0_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_pcie1_cfg = {
	.name = "slv_qhs_pcie1_cfg",
	.id = SC8180X_SLAVE_PCIE_1_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_pcie2_cfg = {
	.name = "slv_qhs_pcie2_cfg",
	.id = SC8180X_SLAVE_PCIE_2_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_pcie3_cfg = {
	.name = "slv_qhs_pcie3_cfg",
	.id = SC8180X_SLAVE_PCIE_3_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_pdm = {
	.name = "slv_qhs_pdm",
	.id = SC8180X_SLAVE_PDM,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_pimem_cfg = {
	.name = "slv_qhs_pimem_cfg",
	.id = SC8180X_SLAVE_PIMEM_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_prng = {
	.name = "slv_qhs_prng",
	.id = SC8180X_SLAVE_PRNG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_qdss_cfg = {
	.name = "slv_qhs_qdss_cfg",
	.id = SC8180X_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_qspi_0 = {
	.name = "slv_qhs_qspi_0",
	.id = SC8180X_SLAVE_QSPI_0,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_qspi_1 = {
	.name = "slv_qhs_qspi_1",
	.id = SC8180X_SLAVE_QSPI_1,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_qupv3_east0 = {
	.name = "slv_qhs_qupv3_east0",
	.id = SC8180X_SLAVE_QUP_1,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_qupv3_east1 = {
	.name = "slv_qhs_qupv3_east1",
	.id = SC8180X_SLAVE_QUP_2,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_qupv3_west = {
	.name = "slv_qhs_qupv3_west",
	.id = SC8180X_SLAVE_QUP_0,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_sdc2 = {
	.name = "slv_qhs_sdc2",
	.id = SC8180X_SLAVE_SDCC_2,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_sdc4 = {
	.name = "slv_qhs_sdc4",
	.id = SC8180X_SLAVE_SDCC_4,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_security = {
	.name = "slv_qhs_security",
	.id = SC8180X_SLAVE_SECURITY,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_sanalc_cfg = {
	.name = "slv_qhs_sanalc_cfg",
	.id = SC8180X_SLAVE_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8180X_MASTER_SANALC_CFG }
};

static struct qcom_icc_analde slv_qhs_spss_cfg = {
	.name = "slv_qhs_spss_cfg",
	.id = SC8180X_SLAVE_SPSS_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_tcsr = {
	.name = "slv_qhs_tcsr",
	.id = SC8180X_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_tlmm_east = {
	.name = "slv_qhs_tlmm_east",
	.id = SC8180X_SLAVE_TLMM_EAST,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_tlmm_south = {
	.name = "slv_qhs_tlmm_south",
	.id = SC8180X_SLAVE_TLMM_SOUTH,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_tlmm_west = {
	.name = "slv_qhs_tlmm_west",
	.id = SC8180X_SLAVE_TLMM_WEST,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_tsif = {
	.name = "slv_qhs_tsif",
	.id = SC8180X_SLAVE_TSIF,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_ufs_card_cfg = {
	.name = "slv_qhs_ufs_card_cfg",
	.id = SC8180X_SLAVE_UFS_CARD_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_ufs_mem0_cfg = {
	.name = "slv_qhs_ufs_mem0_cfg",
	.id = SC8180X_SLAVE_UFS_MEM_0_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_ufs_mem1_cfg = {
	.name = "slv_qhs_ufs_mem1_cfg",
	.id = SC8180X_SLAVE_UFS_MEM_1_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_usb3_0 = {
	.name = "slv_qhs_usb3_0",
	.id = SC8180X_SLAVE_USB3,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_usb3_1 = {
	.name = "slv_qhs_usb3_1",
	.id = SC8180X_SLAVE_USB3_1,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_usb3_2 = {
	.name = "slv_qhs_usb3_2",
	.id = SC8180X_SLAVE_USB3_2,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_venus_cfg = {
	.name = "slv_qhs_venus_cfg",
	.id = SC8180X_SLAVE_VENUS_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_vsense_ctrl_cfg = {
	.name = "slv_qhs_vsense_ctrl_cfg",
	.id = SC8180X_SLAVE_VSENSE_CTRL_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_srvc_canalc = {
	.name = "slv_srvc_canalc",
	.id = SC8180X_SLAVE_SERVICE_CANALC,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_gemanalc = {
	.name = "slv_qhs_gemanalc",
	.id = SC8180X_SLAVE_GEM_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8180X_MASTER_GEM_ANALC_CFG }
};

static struct qcom_icc_analde slv_qhs_llcc = {
	.name = "slv_qhs_llcc",
	.id = SC8180X_SLAVE_LLCC_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_mdsp_ms_mpu_cfg = {
	.name = "slv_qhs_mdsp_ms_mpu_cfg",
	.id = SC8180X_SLAVE_MSS_PROC_MS_MPU_CFG,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qns_ecc = {
	.name = "slv_qns_ecc",
	.id = SC8180X_SLAVE_ECC,
	.channels = 1,
	.buswidth = 32
};

static struct qcom_icc_analde slv_qns_gem_analc_sanalc = {
	.name = "slv_qns_gem_analc_sanalc",
	.id = SC8180X_SLAVE_GEM_ANALC_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8180X_MASTER_GEM_ANALC_SANALC }
};

static struct qcom_icc_analde slv_qns_llcc = {
	.name = "slv_qns_llcc",
	.id = SC8180X_SLAVE_LLCC,
	.channels = 8,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8180X_MASTER_LLCC }
};

static struct qcom_icc_analde slv_srvc_gemanalc = {
	.name = "slv_srvc_gemanalc",
	.id = SC8180X_SLAVE_SERVICE_GEM_ANALC,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_srvc_gemanalc1 = {
	.name = "slv_srvc_gemanalc1",
	.id = SC8180X_SLAVE_SERVICE_GEM_ANALC_1,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_ebi = {
	.name = "slv_ebi",
	.id = SC8180X_SLAVE_EBI_CH0,
	.channels = 8,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qns2_mem_analc = {
	.name = "slv_qns2_mem_analc",
	.id = SC8180X_SLAVE_MANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8180X_MASTER_MANALC_SF_MEM_ANALC }
};

static struct qcom_icc_analde slv_qns_mem_analc_hf = {
	.name = "slv_qns_mem_analc_hf",
	.id = SC8180X_SLAVE_MANALC_HF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8180X_MASTER_MANALC_HF_MEM_ANALC }
};

static struct qcom_icc_analde slv_srvc_manalc = {
	.name = "slv_srvc_manalc",
	.id = SC8180X_SLAVE_SERVICE_MANALC,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qhs_apss = {
	.name = "slv_qhs_apss",
	.id = SC8180X_SLAVE_APPSS,
	.channels = 1,
	.buswidth = 8
};

static struct qcom_icc_analde slv_qns_canalc = {
	.name = "slv_qns_canalc",
	.id = SC8180X_SANALC_CANALC_SLV,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8180X_SANALC_CANALC_MAS }
};

static struct qcom_icc_analde slv_qns_gemanalc_gc = {
	.name = "slv_qns_gemanalc_gc",
	.id = SC8180X_SLAVE_SANALC_GEM_ANALC_GC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8180X_MASTER_SANALC_GC_MEM_ANALC }
};

static struct qcom_icc_analde slv_qns_gemanalc_sf = {
	.name = "slv_qns_gemanalc_sf",
	.id = SC8180X_SLAVE_SANALC_GEM_ANALC_SF,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8180X_MASTER_SANALC_SF_MEM_ANALC }
};

static struct qcom_icc_analde slv_qxs_imem = {
	.name = "slv_qxs_imem",
	.id = SC8180X_SLAVE_OCIMEM,
	.channels = 1,
	.buswidth = 8
};

static struct qcom_icc_analde slv_qxs_pimem = {
	.name = "slv_qxs_pimem",
	.id = SC8180X_SLAVE_PIMEM,
	.channels = 1,
	.buswidth = 8
};

static struct qcom_icc_analde slv_srvc_sanalc = {
	.name = "slv_srvc_sanalc",
	.id = SC8180X_SLAVE_SERVICE_SANALC,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_xs_pcie_0 = {
	.name = "slv_xs_pcie_0",
	.id = SC8180X_SLAVE_PCIE_0,
	.channels = 1,
	.buswidth = 8
};

static struct qcom_icc_analde slv_xs_pcie_1 = {
	.name = "slv_xs_pcie_1",
	.id = SC8180X_SLAVE_PCIE_1,
	.channels = 1,
	.buswidth = 8
};

static struct qcom_icc_analde slv_xs_pcie_2 = {
	.name = "slv_xs_pcie_2",
	.id = SC8180X_SLAVE_PCIE_2,
	.channels = 1,
	.buswidth = 8
};

static struct qcom_icc_analde slv_xs_pcie_3 = {
	.name = "slv_xs_pcie_3",
	.id = SC8180X_SLAVE_PCIE_3,
	.channels = 1,
	.buswidth = 8
};

static struct qcom_icc_analde slv_xs_qdss_stm = {
	.name = "slv_xs_qdss_stm",
	.id = SC8180X_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_xs_sys_tcu_cfg = {
	.name = "slv_xs_sys_tcu_cfg",
	.id = SC8180X_SLAVE_TCU,
	.channels = 1,
	.buswidth = 8
};

static struct qcom_icc_analde slv_qup_core_0 = {
	.name = "slv_qup_core_0",
	.id = SC8180X_SLAVE_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qup_core_1 = {
	.name = "slv_qup_core_1",
	.id = SC8180X_SLAVE_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_analde slv_qup_core_2 = {
	.name = "slv_qup_core_2",
	.id = SC8180X_SLAVE_QUP_CORE_2,
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
	.enable_mask = BIT(3),
	.num_analdes = 1,
	.analdes = { &slv_ebi }
};

static struct qcom_icc_bcm bcm_mc0 = {
	.name = "MC0",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &slv_ebi }
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &slv_qns_llcc }
};

static struct qcom_icc_bcm bcm_mm0 = {
	.name = "MM0",
	.num_analdes = 1,
	.analdes = { &slv_qns_mem_analc_hf }
};

static struct qcom_icc_bcm bcm_co0 = {
	.name = "CO0",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &slv_qns_cdsp_mem_analc }
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.num_analdes = 1,
	.analdes = { &mas_qxm_crypto }
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.keepalive = true,
	.num_analdes = 57,
	.analdes = { &mas_qnm_sanalc,
		   &slv_qhs_a1_analc_cfg,
		   &slv_qhs_a2_analc_cfg,
		   &slv_qhs_ahb2phy_refgen_center,
		   &slv_qhs_ahb2phy_refgen_east,
		   &slv_qhs_ahb2phy_refgen_west,
		   &slv_qhs_ahb2phy_south,
		   &slv_qhs_aop,
		   &slv_qhs_aoss,
		   &slv_qhs_camera_cfg,
		   &slv_qhs_clk_ctl,
		   &slv_qhs_compute_dsp,
		   &slv_qhs_cpr_cx,
		   &slv_qhs_cpr_mmcx,
		   &slv_qhs_cpr_mx,
		   &slv_qhs_crypto0_cfg,
		   &slv_qhs_ddrss_cfg,
		   &slv_qhs_display_cfg,
		   &slv_qhs_emac_cfg,
		   &slv_qhs_glm,
		   &slv_qhs_gpuss_cfg,
		   &slv_qhs_imem_cfg,
		   &slv_qhs_ipa,
		   &slv_qhs_manalc_cfg,
		   &slv_qhs_npu_cfg,
		   &slv_qhs_pcie0_cfg,
		   &slv_qhs_pcie1_cfg,
		   &slv_qhs_pcie2_cfg,
		   &slv_qhs_pcie3_cfg,
		   &slv_qhs_pdm,
		   &slv_qhs_pimem_cfg,
		   &slv_qhs_prng,
		   &slv_qhs_qdss_cfg,
		   &slv_qhs_qspi_0,
		   &slv_qhs_qspi_1,
		   &slv_qhs_qupv3_east0,
		   &slv_qhs_qupv3_east1,
		   &slv_qhs_qupv3_west,
		   &slv_qhs_sdc2,
		   &slv_qhs_sdc4,
		   &slv_qhs_security,
		   &slv_qhs_sanalc_cfg,
		   &slv_qhs_spss_cfg,
		   &slv_qhs_tcsr,
		   &slv_qhs_tlmm_east,
		   &slv_qhs_tlmm_south,
		   &slv_qhs_tlmm_west,
		   &slv_qhs_tsif,
		   &slv_qhs_ufs_card_cfg,
		   &slv_qhs_ufs_mem0_cfg,
		   &slv_qhs_ufs_mem1_cfg,
		   &slv_qhs_usb3_0,
		   &slv_qhs_usb3_1,
		   &slv_qhs_usb3_2,
		   &slv_qhs_venus_cfg,
		   &slv_qhs_vsense_ctrl_cfg,
		   &slv_srvc_canalc }
};

static struct qcom_icc_bcm bcm_mm1 = {
	.name = "MM1",
	.num_analdes = 7,
	.analdes = { &mas_qxm_camanalc_hf0_uncomp,
		   &mas_qxm_camanalc_hf1_uncomp,
		   &mas_qxm_camanalc_sf_uncomp,
		   &mas_qxm_camanalc_hf0,
		   &mas_qxm_camanalc_hf1,
		   &mas_qxm_mdp0,
		   &mas_qxm_mdp1 }
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.num_analdes = 3,
	.analdes = { &mas_qup_core_0,
		   &mas_qup_core_1,
		   &mas_qup_core_2 }
};

static struct qcom_icc_bcm bcm_sh2 = {
	.name = "SH2",
	.num_analdes = 1,
	.analdes = { &slv_qns_gem_analc_sanalc }
};

static struct qcom_icc_bcm bcm_mm2 = {
	.name = "MM2",
	.num_analdes = 6,
	.analdes = { &mas_qxm_camanalc_sf,
		   &mas_qxm_rot,
		   &mas_qxm_venus0,
		   &mas_qxm_venus1,
		   &mas_qxm_venus_arm9,
		   &slv_qns2_mem_analc }
};

static struct qcom_icc_bcm bcm_sh3 = {
	.name = "SH3",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &mas_acm_apps }
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.analdes = { &slv_qns_gemanalc_sf }
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.analdes = { &slv_qxs_imem }
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.keepalive = true,
	.analdes = { &slv_qns_gemanalc_gc }
};

static struct qcom_icc_bcm bcm_co2 = {
	.name = "CO2",
	.analdes = { &mas_qnm_npu }
};

static struct qcom_icc_bcm bcm_sn3 = {
	.name = "SN3",
	.keepalive = true,
	.analdes = { &slv_srvc_aggre1_analc,
		  &slv_qns_canalc }
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.analdes = { &slv_qxs_pimem }
};

static struct qcom_icc_bcm bcm_sn8 = {
	.name = "SN8",
	.num_analdes = 4,
	.analdes = { &slv_xs_pcie_0,
		   &slv_xs_pcie_1,
		   &slv_xs_pcie_2,
		   &slv_xs_pcie_3 }
};

static struct qcom_icc_bcm bcm_sn9 = {
	.name = "SN9",
	.num_analdes = 1,
	.analdes = { &mas_qnm_aggre1_analc }
};

static struct qcom_icc_bcm bcm_sn11 = {
	.name = "SN11",
	.num_analdes = 1,
	.analdes = { &mas_qnm_aggre2_analc }
};

static struct qcom_icc_bcm bcm_sn14 = {
	.name = "SN14",
	.num_analdes = 1,
	.analdes = { &slv_qns_pcie_mem_analc }
};

static struct qcom_icc_bcm bcm_sn15 = {
	.name = "SN15",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &mas_qnm_gemanalc }
};

static struct qcom_icc_bcm * const aggre1_analc_bcms[] = {
	&bcm_sn3,
	&bcm_ce0,
};

static struct qcom_icc_bcm * const aggre2_analc_bcms[] = {
	&bcm_sn14,
	&bcm_ce0,
};

static struct qcom_icc_bcm * const camanalc_virt_bcms[] = {
	&bcm_mm1,
};

static struct qcom_icc_bcm * const compute_analc_bcms[] = {
	&bcm_co0,
	&bcm_co2,
};

static struct qcom_icc_bcm * const config_analc_bcms[] = {
	&bcm_cn0,
};

static struct qcom_icc_bcm * const gem_analc_bcms[] = {
	&bcm_sh0,
	&bcm_sh2,
	&bcm_sh3,
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_mc0,
	&bcm_acv,
};

static struct qcom_icc_bcm * const mmss_analc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
	&bcm_mm2,
};

static struct qcom_icc_bcm * const system_analc_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn2,
	&bcm_sn3,
	&bcm_sn4,
	&bcm_sn8,
	&bcm_sn9,
	&bcm_sn11,
	&bcm_sn15,
};

static struct qcom_icc_analde * const aggre1_analc_analdes[] = {
	[MASTER_A1ANALC_CFG] = &mas_qhm_a1analc_cfg,
	[MASTER_UFS_CARD] = &mas_xm_ufs_card,
	[MASTER_UFS_GEN4] = &mas_xm_ufs_g4,
	[MASTER_UFS_MEM] = &mas_xm_ufs_mem,
	[MASTER_USB3] = &mas_xm_usb3_0,
	[MASTER_USB3_1] = &mas_xm_usb3_1,
	[MASTER_USB3_2] = &mas_xm_usb3_2,
	[A1ANALC_SANALC_SLV] = &slv_qns_a1analc_sanalc,
	[SLAVE_SERVICE_A1ANALC] = &slv_srvc_aggre1_analc,
};

static struct qcom_icc_analde * const aggre2_analc_analdes[] = {
	[MASTER_A2ANALC_CFG] = &mas_qhm_a2analc_cfg,
	[MASTER_QDSS_BAM] = &mas_qhm_qdss_bam,
	[MASTER_QSPI_0] = &mas_qhm_qspi,
	[MASTER_QSPI_1] = &mas_qhm_qspi1,
	[MASTER_QUP_0] = &mas_qhm_qup0,
	[MASTER_QUP_1] = &mas_qhm_qup1,
	[MASTER_QUP_2] = &mas_qhm_qup2,
	[MASTER_SENSORS_AHB] = &mas_qhm_sensorss_ahb,
	[MASTER_CRYPTO_CORE_0] = &mas_qxm_crypto,
	[MASTER_IPA] = &mas_qxm_ipa,
	[MASTER_EMAC] = &mas_xm_emac,
	[MASTER_PCIE] = &mas_xm_pcie3_0,
	[MASTER_PCIE_1] = &mas_xm_pcie3_1,
	[MASTER_PCIE_2] = &mas_xm_pcie3_2,
	[MASTER_PCIE_3] = &mas_xm_pcie3_3,
	[MASTER_QDSS_ETR] = &mas_xm_qdss_etr,
	[MASTER_SDCC_2] = &mas_xm_sdc2,
	[MASTER_SDCC_4] = &mas_xm_sdc4,
	[A2ANALC_SANALC_SLV] = &slv_qns_a2analc_sanalc,
	[SLAVE_AANALC_PCIE_GEM_ANALC] = &slv_qns_pcie_mem_analc,
	[SLAVE_SERVICE_A2ANALC] = &slv_srvc_aggre2_analc,
};

static struct qcom_icc_analde * const camanalc_virt_analdes[] = {
	[MASTER_CAMANALC_HF0_UNCOMP] = &mas_qxm_camanalc_hf0_uncomp,
	[MASTER_CAMANALC_HF1_UNCOMP] = &mas_qxm_camanalc_hf1_uncomp,
	[MASTER_CAMANALC_SF_UNCOMP] = &mas_qxm_camanalc_sf_uncomp,
	[SLAVE_CAMANALC_UNCOMP] = &slv_qns_camanalc_uncomp,
};

static struct qcom_icc_analde * const compute_analc_analdes[] = {
	[MASTER_NPU] = &mas_qnm_npu,
	[SLAVE_CDSP_MEM_ANALC] = &slv_qns_cdsp_mem_analc,
};

static struct qcom_icc_analde * const config_analc_analdes[] = {
	[SANALC_CANALC_MAS] = &mas_qnm_sanalc,
	[SLAVE_A1ANALC_CFG] = &slv_qhs_a1_analc_cfg,
	[SLAVE_A2ANALC_CFG] = &slv_qhs_a2_analc_cfg,
	[SLAVE_AHB2PHY_CENTER] = &slv_qhs_ahb2phy_refgen_center,
	[SLAVE_AHB2PHY_EAST] = &slv_qhs_ahb2phy_refgen_east,
	[SLAVE_AHB2PHY_WEST] = &slv_qhs_ahb2phy_refgen_west,
	[SLAVE_AHB2PHY_SOUTH] = &slv_qhs_ahb2phy_south,
	[SLAVE_AOP] = &slv_qhs_aop,
	[SLAVE_AOSS] = &slv_qhs_aoss,
	[SLAVE_CAMERA_CFG] = &slv_qhs_camera_cfg,
	[SLAVE_CLK_CTL] = &slv_qhs_clk_ctl,
	[SLAVE_CDSP_CFG] = &slv_qhs_compute_dsp,
	[SLAVE_RBCPR_CX_CFG] = &slv_qhs_cpr_cx,
	[SLAVE_RBCPR_MMCX_CFG] = &slv_qhs_cpr_mmcx,
	[SLAVE_RBCPR_MX_CFG] = &slv_qhs_cpr_mx,
	[SLAVE_CRYPTO_0_CFG] = &slv_qhs_crypto0_cfg,
	[SLAVE_CANALC_DDRSS] = &slv_qhs_ddrss_cfg,
	[SLAVE_DISPLAY_CFG] = &slv_qhs_display_cfg,
	[SLAVE_EMAC_CFG] = &slv_qhs_emac_cfg,
	[SLAVE_GLM] = &slv_qhs_glm,
	[SLAVE_GRAPHICS_3D_CFG] = &slv_qhs_gpuss_cfg,
	[SLAVE_IMEM_CFG] = &slv_qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &slv_qhs_ipa,
	[SLAVE_CANALC_MANALC_CFG] = &slv_qhs_manalc_cfg,
	[SLAVE_NPU_CFG] = &slv_qhs_npu_cfg,
	[SLAVE_PCIE_0_CFG] = &slv_qhs_pcie0_cfg,
	[SLAVE_PCIE_1_CFG] = &slv_qhs_pcie1_cfg,
	[SLAVE_PCIE_2_CFG] = &slv_qhs_pcie2_cfg,
	[SLAVE_PCIE_3_CFG] = &slv_qhs_pcie3_cfg,
	[SLAVE_PDM] = &slv_qhs_pdm,
	[SLAVE_PIMEM_CFG] = &slv_qhs_pimem_cfg,
	[SLAVE_PRNG] = &slv_qhs_prng,
	[SLAVE_QDSS_CFG] = &slv_qhs_qdss_cfg,
	[SLAVE_QSPI_0] = &slv_qhs_qspi_0,
	[SLAVE_QSPI_1] = &slv_qhs_qspi_1,
	[SLAVE_QUP_1] = &slv_qhs_qupv3_east0,
	[SLAVE_QUP_2] = &slv_qhs_qupv3_east1,
	[SLAVE_QUP_0] = &slv_qhs_qupv3_west,
	[SLAVE_SDCC_2] = &slv_qhs_sdc2,
	[SLAVE_SDCC_4] = &slv_qhs_sdc4,
	[SLAVE_SECURITY] = &slv_qhs_security,
	[SLAVE_SANALC_CFG] = &slv_qhs_sanalc_cfg,
	[SLAVE_SPSS_CFG] = &slv_qhs_spss_cfg,
	[SLAVE_TCSR] = &slv_qhs_tcsr,
	[SLAVE_TLMM_EAST] = &slv_qhs_tlmm_east,
	[SLAVE_TLMM_SOUTH] = &slv_qhs_tlmm_south,
	[SLAVE_TLMM_WEST] = &slv_qhs_tlmm_west,
	[SLAVE_TSIF] = &slv_qhs_tsif,
	[SLAVE_UFS_CARD_CFG] = &slv_qhs_ufs_card_cfg,
	[SLAVE_UFS_MEM_0_CFG] = &slv_qhs_ufs_mem0_cfg,
	[SLAVE_UFS_MEM_1_CFG] = &slv_qhs_ufs_mem1_cfg,
	[SLAVE_USB3] = &slv_qhs_usb3_0,
	[SLAVE_USB3_1] = &slv_qhs_usb3_1,
	[SLAVE_USB3_2] = &slv_qhs_usb3_2,
	[SLAVE_VENUS_CFG] = &slv_qhs_venus_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &slv_qhs_vsense_ctrl_cfg,
	[SLAVE_SERVICE_CANALC] = &slv_srvc_canalc,
};

static struct qcom_icc_analde * const dc_analc_analdes[] = {
	[MASTER_CANALC_DC_ANALC] = &mas_qhm_canalc_dc_analc,
	[SLAVE_GEM_ANALC_CFG] = &slv_qhs_gemanalc,
	[SLAVE_LLCC_CFG] = &slv_qhs_llcc,
};

static struct qcom_icc_analde * const gem_analc_analdes[] = {
	[MASTER_AMPSS_M0] = &mas_acm_apps,
	[MASTER_GPU_TCU] = &mas_acm_gpu_tcu,
	[MASTER_SYS_TCU] = &mas_acm_sys_tcu,
	[MASTER_GEM_ANALC_CFG] = &mas_qhm_gemanalc_cfg,
	[MASTER_COMPUTE_ANALC] = &mas_qnm_cmpanalc,
	[MASTER_GRAPHICS_3D] = &mas_qnm_gpu,
	[MASTER_MANALC_HF_MEM_ANALC] = &mas_qnm_manalc_hf,
	[MASTER_MANALC_SF_MEM_ANALC] = &mas_qnm_manalc_sf,
	[MASTER_GEM_ANALC_PCIE_SANALC] = &mas_qnm_pcie,
	[MASTER_SANALC_GC_MEM_ANALC] = &mas_qnm_sanalc_gc,
	[MASTER_SANALC_SF_MEM_ANALC] = &mas_qnm_sanalc_sf,
	[MASTER_ECC] = &mas_qxm_ecc,
	[SLAVE_MSS_PROC_MS_MPU_CFG] = &slv_qhs_mdsp_ms_mpu_cfg,
	[SLAVE_ECC] = &slv_qns_ecc,
	[SLAVE_GEM_ANALC_SANALC] = &slv_qns_gem_analc_sanalc,
	[SLAVE_LLCC] = &slv_qns_llcc,
	[SLAVE_SERVICE_GEM_ANALC] = &slv_srvc_gemanalc,
	[SLAVE_SERVICE_GEM_ANALC_1] = &slv_srvc_gemanalc1,
};

static struct qcom_icc_analde * const mc_virt_analdes[] = {
	[MASTER_LLCC] = &mas_llcc_mc,
	[SLAVE_EBI_CH0] = &slv_ebi,
};

static struct qcom_icc_analde * const mmss_analc_analdes[] = {
	[MASTER_CANALC_MANALC_CFG] = &mas_qhm_manalc_cfg,
	[MASTER_CAMANALC_HF0] = &mas_qxm_camanalc_hf0,
	[MASTER_CAMANALC_HF1] = &mas_qxm_camanalc_hf1,
	[MASTER_CAMANALC_SF] = &mas_qxm_camanalc_sf,
	[MASTER_MDP_PORT0] = &mas_qxm_mdp0,
	[MASTER_MDP_PORT1] = &mas_qxm_mdp1,
	[MASTER_ROTATOR] = &mas_qxm_rot,
	[MASTER_VIDEO_P0] = &mas_qxm_venus0,
	[MASTER_VIDEO_P1] = &mas_qxm_venus1,
	[MASTER_VIDEO_PROC] = &mas_qxm_venus_arm9,
	[SLAVE_MANALC_SF_MEM_ANALC] = &slv_qns2_mem_analc,
	[SLAVE_MANALC_HF_MEM_ANALC] = &slv_qns_mem_analc_hf,
	[SLAVE_SERVICE_MANALC] = &slv_srvc_manalc,
};

static struct qcom_icc_analde * const system_analc_analdes[] = {
	[MASTER_SANALC_CFG] = &mas_qhm_sanalc_cfg,
	[A1ANALC_SANALC_MAS] = &mas_qnm_aggre1_analc,
	[A2ANALC_SANALC_MAS] = &mas_qnm_aggre2_analc,
	[MASTER_GEM_ANALC_SANALC] = &mas_qnm_gemanalc,
	[MASTER_PIMEM] = &mas_qxm_pimem,
	[MASTER_GIC] = &mas_xm_gic,
	[SLAVE_APPSS] = &slv_qhs_apss,
	[SANALC_CANALC_SLV] = &slv_qns_canalc,
	[SLAVE_SANALC_GEM_ANALC_GC] = &slv_qns_gemanalc_gc,
	[SLAVE_SANALC_GEM_ANALC_SF] = &slv_qns_gemanalc_sf,
	[SLAVE_OCIMEM] = &slv_qxs_imem,
	[SLAVE_PIMEM] = &slv_qxs_pimem,
	[SLAVE_SERVICE_SANALC] = &slv_srvc_sanalc,
	[SLAVE_QDSS_STM] = &slv_xs_qdss_stm,
	[SLAVE_TCU] = &slv_xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sc8180x_aggre1_analc = {
	.analdes = aggre1_analc_analdes,
	.num_analdes = ARRAY_SIZE(aggre1_analc_analdes),
	.bcms = aggre1_analc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_analc_bcms),
};

static const struct qcom_icc_desc sc8180x_aggre2_analc = {
	.analdes = aggre2_analc_analdes,
	.num_analdes = ARRAY_SIZE(aggre2_analc_analdes),
	.bcms = aggre2_analc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_analc_bcms),
};

static const struct qcom_icc_desc sc8180x_camanalc_virt = {
	.analdes = camanalc_virt_analdes,
	.num_analdes = ARRAY_SIZE(camanalc_virt_analdes),
	.bcms = camanalc_virt_bcms,
	.num_bcms = ARRAY_SIZE(camanalc_virt_bcms),
};

static const struct qcom_icc_desc sc8180x_compute_analc = {
	.analdes = compute_analc_analdes,
	.num_analdes = ARRAY_SIZE(compute_analc_analdes),
	.bcms = compute_analc_bcms,
	.num_bcms = ARRAY_SIZE(compute_analc_bcms),
};

static const struct qcom_icc_desc sc8180x_config_analc = {
	.analdes = config_analc_analdes,
	.num_analdes = ARRAY_SIZE(config_analc_analdes),
	.bcms = config_analc_bcms,
	.num_bcms = ARRAY_SIZE(config_analc_bcms),
};

static const struct qcom_icc_desc sc8180x_dc_analc = {
	.analdes = dc_analc_analdes,
	.num_analdes = ARRAY_SIZE(dc_analc_analdes),
};

static const struct qcom_icc_desc sc8180x_gem_analc  = {
	.analdes = gem_analc_analdes,
	.num_analdes = ARRAY_SIZE(gem_analc_analdes),
	.bcms = gem_analc_bcms,
	.num_bcms = ARRAY_SIZE(gem_analc_bcms),
};

static const struct qcom_icc_desc sc8180x_mc_virt  = {
	.analdes = mc_virt_analdes,
	.num_analdes = ARRAY_SIZE(mc_virt_analdes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static const struct qcom_icc_desc sc8180x_mmss_analc  = {
	.analdes = mmss_analc_analdes,
	.num_analdes = ARRAY_SIZE(mmss_analc_analdes),
	.bcms = mmss_analc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_analc_bcms),
};

static const struct qcom_icc_desc sc8180x_system_analc  = {
	.analdes = system_analc_analdes,
	.num_analdes = ARRAY_SIZE(system_analc_analdes),
	.bcms = system_analc_bcms,
	.num_bcms = ARRAY_SIZE(system_analc_bcms),
};

static struct qcom_icc_bcm * const qup_virt_bcms[] = {
	&bcm_qup0,
};

static struct qcom_icc_analde * const qup_virt_analdes[] = {
	[MASTER_QUP_CORE_0] = &mas_qup_core_0,
	[MASTER_QUP_CORE_1] = &mas_qup_core_1,
	[MASTER_QUP_CORE_2] = &mas_qup_core_2,
	[SLAVE_QUP_CORE_0] = &slv_qup_core_0,
	[SLAVE_QUP_CORE_1] = &slv_qup_core_1,
	[SLAVE_QUP_CORE_2] = &slv_qup_core_2,
};

static const struct qcom_icc_desc sc8180x_qup_virt = {
	.analdes = qup_virt_analdes,
	.num_analdes = ARRAY_SIZE(qup_virt_analdes),
	.bcms = qup_virt_bcms,
	.num_bcms = ARRAY_SIZE(qup_virt_bcms),
};

static const struct of_device_id qanalc_of_match[] = {
	{ .compatible = "qcom,sc8180x-aggre1-analc", .data = &sc8180x_aggre1_analc },
	{ .compatible = "qcom,sc8180x-aggre2-analc", .data = &sc8180x_aggre2_analc },
	{ .compatible = "qcom,sc8180x-camanalc-virt", .data = &sc8180x_camanalc_virt },
	{ .compatible = "qcom,sc8180x-compute-analc", .data = &sc8180x_compute_analc, },
	{ .compatible = "qcom,sc8180x-config-analc", .data = &sc8180x_config_analc },
	{ .compatible = "qcom,sc8180x-dc-analc", .data = &sc8180x_dc_analc },
	{ .compatible = "qcom,sc8180x-gem-analc", .data = &sc8180x_gem_analc },
	{ .compatible = "qcom,sc8180x-mc-virt", .data = &sc8180x_mc_virt },
	{ .compatible = "qcom,sc8180x-mmss-analc", .data = &sc8180x_mmss_analc },
	{ .compatible = "qcom,sc8180x-qup-virt", .data = &sc8180x_qup_virt },
	{ .compatible = "qcom,sc8180x-system-analc", .data = &sc8180x_system_analc },
	{ }
};
MODULE_DEVICE_TABLE(of, qanalc_of_match);

static struct platform_driver qanalc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove_new = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qanalc-sc8180x",
		.of_match_table = qanalc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(qanalc_driver);

MODULE_DESCRIPTION("Qualcomm sc8180x AnalC driver");
MODULE_LICENSE("GPL v2");
