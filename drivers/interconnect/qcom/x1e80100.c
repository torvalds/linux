// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Inanalvation Center, Inc. All rights reserved.
 * Copyright (c) 2023, Linaro Limited
 *
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <dt-bindings/interconnect/qcom,x1e80100-rpmh.h>

#include "bcm-voter.h"
#include "icc-common.h"
#include "icc-rpmh.h"
#include "x1e80100.h"

static struct qcom_icc_analde qhm_qspi = {
	.name = "qhm_qspi",
	.id = X1E80100_MASTER_QSPI_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { X1E80100_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qup1 = {
	.name = "qhm_qup1",
	.id = X1E80100_MASTER_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { X1E80100_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_sdc4 = {
	.name = "xm_sdc4",
	.id = X1E80100_MASTER_SDCC_4,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { X1E80100_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.id = X1E80100_MASTER_UFS_MEM,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qup0 = {
	.name = "qhm_qup0",
	.id = X1E80100_MASTER_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { X1E80100_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qup2 = {
	.name = "qhm_qup2",
	.id = X1E80100_MASTER_QUP_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { X1E80100_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qxm_crypto = {
	.name = "qxm_crypto",
	.id = X1E80100_MASTER_CRYPTO,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { X1E80100_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qxm_sp = {
	.name = "qxm_sp",
	.id = X1E80100_MASTER_SP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { X1E80100_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde xm_qdss_etr_0 = {
	.name = "xm_qdss_etr_0",
	.id = X1E80100_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { X1E80100_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde xm_qdss_etr_1 = {
	.name = "xm_qdss_etr_1",
	.id = X1E80100_MASTER_QDSS_ETR_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { X1E80100_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde xm_sdc2 = {
	.name = "xm_sdc2",
	.id = X1E80100_MASTER_SDCC_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { X1E80100_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde ddr_perf_mode_master = {
	.name = "ddr_perf_mode_master",
	.id = X1E80100_MASTER_DDR_PERF_MODE,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { X1E80100_SLAVE_DDR_PERF_MODE },
};

static struct qcom_icc_analde qup0_core_master = {
	.name = "qup0_core_master",
	.id = X1E80100_MASTER_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { X1E80100_SLAVE_QUP_CORE_0 },
};

static struct qcom_icc_analde qup1_core_master = {
	.name = "qup1_core_master",
	.id = X1E80100_MASTER_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { X1E80100_SLAVE_QUP_CORE_1 },
};

static struct qcom_icc_analde qup2_core_master = {
	.name = "qup2_core_master",
	.id = X1E80100_MASTER_QUP_CORE_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { X1E80100_SLAVE_QUP_CORE_2 },
};

static struct qcom_icc_analde qsm_cfg = {
	.name = "qsm_cfg",
	.id = X1E80100_MASTER_CANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 47,
	.links = { X1E80100_SLAVE_AHB2PHY_SOUTH, X1E80100_SLAVE_AHB2PHY_ANALRTH,
		   X1E80100_SLAVE_AHB2PHY_2, X1E80100_SLAVE_AV1_ENC_CFG,
		   X1E80100_SLAVE_CAMERA_CFG, X1E80100_SLAVE_CLK_CTL,
		   X1E80100_SLAVE_CRYPTO_0_CFG, X1E80100_SLAVE_DISPLAY_CFG,
		   X1E80100_SLAVE_GFX3D_CFG, X1E80100_SLAVE_IMEM_CFG,
		   X1E80100_SLAVE_IPC_ROUTER_CFG, X1E80100_SLAVE_PCIE_0_CFG,
		   X1E80100_SLAVE_PCIE_1_CFG, X1E80100_SLAVE_PCIE_2_CFG,
		   X1E80100_SLAVE_PCIE_3_CFG, X1E80100_SLAVE_PCIE_4_CFG,
		   X1E80100_SLAVE_PCIE_5_CFG, X1E80100_SLAVE_PCIE_6A_CFG,
		   X1E80100_SLAVE_PCIE_6B_CFG, X1E80100_SLAVE_PCIE_RSC_CFG,
		   X1E80100_SLAVE_PDM, X1E80100_SLAVE_PRNG,
		   X1E80100_SLAVE_QDSS_CFG, X1E80100_SLAVE_QSPI_0,
		   X1E80100_SLAVE_QUP_0, X1E80100_SLAVE_QUP_1,
		   X1E80100_SLAVE_QUP_2, X1E80100_SLAVE_SDCC_2,
		   X1E80100_SLAVE_SDCC_4, X1E80100_SLAVE_SMMUV3_CFG,
		   X1E80100_SLAVE_TCSR, X1E80100_SLAVE_TLMM,
		   X1E80100_SLAVE_UFS_MEM_CFG, X1E80100_SLAVE_USB2,
		   X1E80100_SLAVE_USB3_0, X1E80100_SLAVE_USB3_1,
		   X1E80100_SLAVE_USB3_2, X1E80100_SLAVE_USB3_MP,
		   X1E80100_SLAVE_USB4_0, X1E80100_SLAVE_USB4_1,
		   X1E80100_SLAVE_USB4_2, X1E80100_SLAVE_VENUS_CFG,
		   X1E80100_SLAVE_LPASS_QTB_CFG, X1E80100_SLAVE_CANALC_MANALC_CFG,
		   X1E80100_SLAVE_NSP_QTB_CFG, X1E80100_SLAVE_QDSS_STM,
		   X1E80100_SLAVE_TCU },
};

static struct qcom_icc_analde qnm_gemanalc_canalc = {
	.name = "qnm_gemanalc_canalc",
	.id = X1E80100_MASTER_GEM_ANALC_CANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 6,
	.links = { X1E80100_SLAVE_AOSS, X1E80100_SLAVE_TME_CFG,
		   X1E80100_SLAVE_APPSS, X1E80100_SLAVE_CANALC_CFG,
		   X1E80100_SLAVE_BOOT_IMEM, X1E80100_SLAVE_IMEM },
};

static struct qcom_icc_analde qnm_gemanalc_pcie = {
	.name = "qnm_gemanalc_pcie",
	.id = X1E80100_MASTER_GEM_ANALC_PCIE_SANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 8,
	.links = { X1E80100_SLAVE_PCIE_0, X1E80100_SLAVE_PCIE_1,
		   X1E80100_SLAVE_PCIE_2, X1E80100_SLAVE_PCIE_3,
		   X1E80100_SLAVE_PCIE_4, X1E80100_SLAVE_PCIE_5,
		   X1E80100_SLAVE_PCIE_6A, X1E80100_SLAVE_PCIE_6B },
};

static struct qcom_icc_analde alm_gpu_tcu = {
	.name = "alm_gpu_tcu",
	.id = X1E80100_MASTER_GPU_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { X1E80100_SLAVE_GEM_ANALC_CANALC, X1E80100_SLAVE_LLCC },
};

static struct qcom_icc_analde alm_pcie_tcu = {
	.name = "alm_pcie_tcu",
	.id = X1E80100_MASTER_PCIE_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { X1E80100_SLAVE_GEM_ANALC_CANALC, X1E80100_SLAVE_LLCC },
};

static struct qcom_icc_analde alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.id = X1E80100_MASTER_SYS_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { X1E80100_SLAVE_GEM_ANALC_CANALC, X1E80100_SLAVE_LLCC },
};

static struct qcom_icc_analde chm_apps = {
	.name = "chm_apps",
	.id = X1E80100_MASTER_APPSS_PROC,
	.channels = 6,
	.buswidth = 32,
	.num_links = 3,
	.links = { X1E80100_SLAVE_GEM_ANALC_CANALC, X1E80100_SLAVE_LLCC,
		   X1E80100_SLAVE_MEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde qnm_gpu = {
	.name = "qnm_gpu",
	.id = X1E80100_MASTER_GFX3D,
	.channels = 4,
	.buswidth = 32,
	.num_links = 2,
	.links = { X1E80100_SLAVE_GEM_ANALC_CANALC, X1E80100_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_lpass = {
	.name = "qnm_lpass",
	.id = X1E80100_MASTER_LPASS_GEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { X1E80100_SLAVE_GEM_ANALC_CANALC, X1E80100_SLAVE_LLCC,
		   X1E80100_SLAVE_MEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde qnm_manalc_hf = {
	.name = "qnm_manalc_hf",
	.id = X1E80100_MASTER_MANALC_HF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { X1E80100_SLAVE_GEM_ANALC_CANALC, X1E80100_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_manalc_sf = {
	.name = "qnm_manalc_sf",
	.id = X1E80100_MASTER_MANALC_SF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { X1E80100_SLAVE_GEM_ANALC_CANALC, X1E80100_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_nsp_analc = {
	.name = "qnm_nsp_analc",
	.id = X1E80100_MASTER_COMPUTE_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 3,
	.links = { X1E80100_SLAVE_GEM_ANALC_CANALC, X1E80100_SLAVE_LLCC,
		   X1E80100_SLAVE_MEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde qnm_pcie = {
	.name = "qnm_pcie",
	.id = X1E80100_MASTER_AANALC_PCIE_GEM_ANALC,
	.channels = 1,
	.buswidth = 64,
	.num_links = 2,
	.links = { X1E80100_SLAVE_GEM_ANALC_CANALC, X1E80100_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_sanalc_sf = {
	.name = "qnm_sanalc_sf",
	.id = X1E80100_MASTER_SANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 64,
	.num_links = 3,
	.links = { X1E80100_SLAVE_GEM_ANALC_CANALC, X1E80100_SLAVE_LLCC,
		   X1E80100_SLAVE_MEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde xm_gic = {
	.name = "xm_gic",
	.id = X1E80100_MASTER_GIC2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { X1E80100_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_lpiaon_analc = {
	.name = "qnm_lpiaon_analc",
	.id = X1E80100_MASTER_LPIAON_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_SLAVE_LPASS_GEM_ANALC },
};

static struct qcom_icc_analde qnm_lpass_lpianalc = {
	.name = "qnm_lpass_lpianalc",
	.id = X1E80100_MASTER_LPASS_LPIANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_SLAVE_LPIAON_ANALC_LPASS_AG_ANALC },
};

static struct qcom_icc_analde qxm_lpianalc_dsp_axim = {
	.name = "qxm_lpianalc_dsp_axim",
	.id = X1E80100_MASTER_LPASS_PROC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_SLAVE_LPICX_ANALC_LPIAON_ANALC },
};

static struct qcom_icc_analde llcc_mc = {
	.name = "llcc_mc",
	.id = X1E80100_MASTER_LLCC,
	.channels = 8,
	.buswidth = 4,
	.num_links = 1,
	.links = { X1E80100_SLAVE_EBI1 },
};

static struct qcom_icc_analde qnm_av1_enc = {
	.name = "qnm_av1_enc",
	.id = X1E80100_MASTER_AV1_ENC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { X1E80100_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_camanalc_hf = {
	.name = "qnm_camanalc_hf",
	.id = X1E80100_MASTER_CAMANALC_HF,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { X1E80100_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_camanalc_icp = {
	.name = "qnm_camanalc_icp",
	.id = X1E80100_MASTER_CAMANALC_ICP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { X1E80100_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_camanalc_sf = {
	.name = "qnm_camanalc_sf",
	.id = X1E80100_MASTER_CAMANALC_SF,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { X1E80100_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_eva = {
	.name = "qnm_eva",
	.id = X1E80100_MASTER_EVA,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { X1E80100_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_mdp = {
	.name = "qnm_mdp",
	.id = X1E80100_MASTER_MDP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { X1E80100_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_video = {
	.name = "qnm_video",
	.id = X1E80100_MASTER_VIDEO,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { X1E80100_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_video_cv_cpu = {
	.name = "qnm_video_cv_cpu",
	.id = X1E80100_MASTER_VIDEO_CV_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { X1E80100_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_video_v_cpu = {
	.name = "qnm_video_v_cpu",
	.id = X1E80100_MASTER_VIDEO_V_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { X1E80100_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qsm_manalc_cfg = {
	.name = "qsm_manalc_cfg",
	.id = X1E80100_MASTER_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { X1E80100_SLAVE_SERVICE_MANALC },
};

static struct qcom_icc_analde qxm_nsp = {
	.name = "qxm_nsp",
	.id = X1E80100_MASTER_CDSP_PROC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { X1E80100_SLAVE_CDSP_MEM_ANALC },
};

static struct qcom_icc_analde qnm_pcie_analrth_gem_analc = {
	.name = "qnm_pcie_analrth_gem_analc",
	.id = X1E80100_MASTER_PCIE_ANALRTH,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { X1E80100_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde qnm_pcie_south_gem_analc = {
	.name = "qnm_pcie_south_gem_analc",
	.id = X1E80100_MASTER_PCIE_SOUTH,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { X1E80100_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde xm_pcie_3 = {
	.name = "xm_pcie_3",
	.id = X1E80100_MASTER_PCIE_3,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { X1E80100_SLAVE_PCIE_ANALRTH },
};

static struct qcom_icc_analde xm_pcie_4 = {
	.name = "xm_pcie_4",
	.id = X1E80100_MASTER_PCIE_4,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { X1E80100_SLAVE_PCIE_ANALRTH },
};

static struct qcom_icc_analde xm_pcie_5 = {
	.name = "xm_pcie_5",
	.id = X1E80100_MASTER_PCIE_5,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { X1E80100_SLAVE_PCIE_ANALRTH },
};

static struct qcom_icc_analde xm_pcie_0 = {
	.name = "xm_pcie_0",
	.id = X1E80100_MASTER_PCIE_0,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_SLAVE_PCIE_SOUTH },
};

static struct qcom_icc_analde xm_pcie_1 = {
	.name = "xm_pcie_1",
	.id = X1E80100_MASTER_PCIE_1,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_SLAVE_PCIE_SOUTH },
};

static struct qcom_icc_analde xm_pcie_2 = {
	.name = "xm_pcie_2",
	.id = X1E80100_MASTER_PCIE_2,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_SLAVE_PCIE_SOUTH },
};

static struct qcom_icc_analde xm_pcie_6a = {
	.name = "xm_pcie_6a",
	.id = X1E80100_MASTER_PCIE_6A,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { X1E80100_SLAVE_PCIE_SOUTH },
};

static struct qcom_icc_analde xm_pcie_6b = {
	.name = "xm_pcie_6b",
	.id = X1E80100_MASTER_PCIE_6B,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_SLAVE_PCIE_SOUTH },
};

static struct qcom_icc_analde qnm_aggre1_analc = {
	.name = "qnm_aggre1_analc",
	.id = X1E80100_MASTER_A1ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qnm_aggre2_analc = {
	.name = "qnm_aggre2_analc",
	.id = X1E80100_MASTER_A2ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qnm_gic = {
	.name = "qnm_gic",
	.id = X1E80100_MASTER_GIC1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { X1E80100_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qnm_usb_aanalc = {
	.name = "qnm_usb_aanalc",
	.id = X1E80100_MASTER_USB_ANALC_SANALC,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { X1E80100_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qnm_aggre_usb_analrth_sanalc = {
	.name = "qnm_aggre_usb_analrth_sanalc",
	.id = X1E80100_MASTER_AGGRE_USB_ANALRTH,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { X1E80100_SLAVE_USB_ANALC_SANALC },
};

static struct qcom_icc_analde qnm_aggre_usb_south_sanalc = {
	.name = "qnm_aggre_usb_south_sanalc",
	.id = X1E80100_MASTER_AGGRE_USB_SOUTH,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { X1E80100_SLAVE_USB_ANALC_SANALC },
};

static struct qcom_icc_analde xm_usb2_0 = {
	.name = "xm_usb2_0",
	.id = X1E80100_MASTER_USB2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { X1E80100_SLAVE_AGGRE_USB_ANALRTH },
};

static struct qcom_icc_analde xm_usb3_mp = {
	.name = "xm_usb3_mp",
	.id = X1E80100_MASTER_USB3_MP,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_SLAVE_AGGRE_USB_ANALRTH },
};

static struct qcom_icc_analde xm_usb3_0 = {
	.name = "xm_usb3_0",
	.id = X1E80100_MASTER_USB3_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { X1E80100_SLAVE_AGGRE_USB_SOUTH },
};

static struct qcom_icc_analde xm_usb3_1 = {
	.name = "xm_usb3_1",
	.id = X1E80100_MASTER_USB3_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { X1E80100_SLAVE_AGGRE_USB_SOUTH },
};

static struct qcom_icc_analde xm_usb3_2 = {
	.name = "xm_usb3_2",
	.id = X1E80100_MASTER_USB3_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { X1E80100_SLAVE_AGGRE_USB_SOUTH },
};

static struct qcom_icc_analde xm_usb4_0 = {
	.name = "xm_usb4_0",
	.id = X1E80100_MASTER_USB4_0,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_SLAVE_AGGRE_USB_SOUTH },
};

static struct qcom_icc_analde xm_usb4_1 = {
	.name = "xm_usb4_1",
	.id = X1E80100_MASTER_USB4_1,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_SLAVE_AGGRE_USB_SOUTH },
};

static struct qcom_icc_analde xm_usb4_2 = {
	.name = "xm_usb4_2",
	.id = X1E80100_MASTER_USB4_2,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_SLAVE_AGGRE_USB_SOUTH },
};

static struct qcom_icc_analde qnm_manalc_hf_disp = {
	.name = "qnm_manalc_hf_disp",
	.id = X1E80100_MASTER_MANALC_HF_MEM_ANALC_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { X1E80100_SLAVE_LLCC_DISP },
};

static struct qcom_icc_analde qnm_pcie_disp = {
	.name = "qnm_pcie_disp",
	.id = X1E80100_MASTER_AANALC_PCIE_GEM_ANALC_DISP,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { X1E80100_SLAVE_LLCC_DISP },
};

static struct qcom_icc_analde llcc_mc_disp = {
	.name = "llcc_mc_disp",
	.id = X1E80100_MASTER_LLCC_DISP,
	.channels = 8,
	.buswidth = 4,
	.num_links = 1,
	.links = { X1E80100_SLAVE_EBI1_DISP },
};

static struct qcom_icc_analde qnm_mdp_disp = {
	.name = "qnm_mdp_disp",
	.id = X1E80100_MASTER_MDP_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { X1E80100_SLAVE_MANALC_HF_MEM_ANALC_DISP },
};

static struct qcom_icc_analde qnm_pcie_pcie = {
	.name = "qnm_pcie_pcie",
	.id = X1E80100_MASTER_AANALC_PCIE_GEM_ANALC_PCIE,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { X1E80100_SLAVE_LLCC_PCIE },
};

static struct qcom_icc_analde llcc_mc_pcie = {
	.name = "llcc_mc_pcie",
	.id = X1E80100_MASTER_LLCC_PCIE,
	.channels = 8,
	.buswidth = 4,
	.num_links = 1,
	.links = { X1E80100_SLAVE_EBI1_PCIE },
};

static struct qcom_icc_analde qnm_pcie_analrth_gem_analc_pcie = {
	.name = "qnm_pcie_analrth_gem_analc_pcie",
	.id = X1E80100_MASTER_PCIE_ANALRTH_PCIE,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { X1E80100_SLAVE_AANALC_PCIE_GEM_ANALC_PCIE },
};

static struct qcom_icc_analde qnm_pcie_south_gem_analc_pcie = {
	.name = "qnm_pcie_south_gem_analc_pcie",
	.id = X1E80100_MASTER_PCIE_SOUTH_PCIE,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { X1E80100_SLAVE_AANALC_PCIE_GEM_ANALC_PCIE },
};

static struct qcom_icc_analde xm_pcie_3_pcie = {
	.name = "xm_pcie_3_pcie",
	.id = X1E80100_MASTER_PCIE_3_PCIE,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { X1E80100_SLAVE_PCIE_ANALRTH_PCIE },
};

static struct qcom_icc_analde xm_pcie_4_pcie = {
	.name = "xm_pcie_4_pcie",
	.id = X1E80100_MASTER_PCIE_4_PCIE,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { X1E80100_SLAVE_PCIE_ANALRTH_PCIE },
};

static struct qcom_icc_analde xm_pcie_5_pcie = {
	.name = "xm_pcie_5_pcie",
	.id = X1E80100_MASTER_PCIE_5_PCIE,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { X1E80100_SLAVE_PCIE_ANALRTH_PCIE },
};

static struct qcom_icc_analde xm_pcie_0_pcie = {
	.name = "xm_pcie_0_pcie",
	.id = X1E80100_MASTER_PCIE_0_PCIE,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_SLAVE_PCIE_SOUTH_PCIE },
};

static struct qcom_icc_analde xm_pcie_1_pcie = {
	.name = "xm_pcie_1_pcie",
	.id = X1E80100_MASTER_PCIE_1_PCIE,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_SLAVE_PCIE_SOUTH_PCIE },
};

static struct qcom_icc_analde xm_pcie_2_pcie = {
	.name = "xm_pcie_2_pcie",
	.id = X1E80100_MASTER_PCIE_2_PCIE,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_SLAVE_PCIE_SOUTH_PCIE },
};

static struct qcom_icc_analde xm_pcie_6a_pcie = {
	.name = "xm_pcie_6a_pcie",
	.id = X1E80100_MASTER_PCIE_6A_PCIE,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { X1E80100_SLAVE_PCIE_SOUTH_PCIE },
};

static struct qcom_icc_analde xm_pcie_6b_pcie = {
	.name = "xm_pcie_6b_pcie",
	.id = X1E80100_MASTER_PCIE_6B_PCIE,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_SLAVE_PCIE_SOUTH_PCIE },
};

static struct qcom_icc_analde qns_a1analc_sanalc = {
	.name = "qns_a1analc_sanalc",
	.id = X1E80100_SLAVE_A1ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_MASTER_A1ANALC_SANALC },
};

static struct qcom_icc_analde qns_a2analc_sanalc = {
	.name = "qns_a2analc_sanalc",
	.id = X1E80100_SLAVE_A2ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_MASTER_A2ANALC_SANALC },
};

static struct qcom_icc_analde ddr_perf_mode_slave = {
	.name = "ddr_perf_mode_slave",
	.id = X1E80100_SLAVE_DDR_PERF_MODE,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qup0_core_slave = {
	.name = "qup0_core_slave",
	.id = X1E80100_SLAVE_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qup1_core_slave = {
	.name = "qup1_core_slave",
	.id = X1E80100_SLAVE_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qup2_core_slave = {
	.name = "qup2_core_slave",
	.id = X1E80100_SLAVE_QUP_CORE_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ahb2phy0 = {
	.name = "qhs_ahb2phy0",
	.id = X1E80100_SLAVE_AHB2PHY_SOUTH,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ahb2phy1 = {
	.name = "qhs_ahb2phy1",
	.id = X1E80100_SLAVE_AHB2PHY_ANALRTH,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ahb2phy2 = {
	.name = "qhs_ahb2phy2",
	.id = X1E80100_SLAVE_AHB2PHY_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_av1_enc_cfg = {
	.name = "qhs_av1_enc_cfg",
	.id = X1E80100_SLAVE_AV1_ENC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_camera_cfg = {
	.name = "qhs_camera_cfg",
	.id = X1E80100_SLAVE_CAMERA_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = X1E80100_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = X1E80100_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_display_cfg = {
	.name = "qhs_display_cfg",
	.id = X1E80100_SLAVE_DISPLAY_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.id = X1E80100_SLAVE_GFX3D_CFG,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = X1E80100_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ipc_router = {
	.name = "qhs_ipc_router",
	.id = X1E80100_SLAVE_IPC_ROUTER_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pcie0_cfg = {
	.name = "qhs_pcie0_cfg",
	.id = X1E80100_SLAVE_PCIE_0_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pcie1_cfg = {
	.name = "qhs_pcie1_cfg",
	.id = X1E80100_SLAVE_PCIE_1_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pcie2_cfg = {
	.name = "qhs_pcie2_cfg",
	.id = X1E80100_SLAVE_PCIE_2_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pcie3_cfg = {
	.name = "qhs_pcie3_cfg",
	.id = X1E80100_SLAVE_PCIE_3_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pcie4_cfg = {
	.name = "qhs_pcie4_cfg",
	.id = X1E80100_SLAVE_PCIE_4_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pcie5_cfg = {
	.name = "qhs_pcie5_cfg",
	.id = X1E80100_SLAVE_PCIE_5_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pcie6a_cfg = {
	.name = "qhs_pcie6a_cfg",
	.id = X1E80100_SLAVE_PCIE_6A_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pcie6b_cfg = {
	.name = "qhs_pcie6b_cfg",
	.id = X1E80100_SLAVE_PCIE_6B_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pcie_rsc_cfg = {
	.name = "qhs_pcie_rsc_cfg",
	.id = X1E80100_SLAVE_PCIE_RSC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pdm = {
	.name = "qhs_pdm",
	.id = X1E80100_SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_prng = {
	.name = "qhs_prng",
	.id = X1E80100_SLAVE_PRNG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = X1E80100_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qspi = {
	.name = "qhs_qspi",
	.id = X1E80100_SLAVE_QSPI_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qup0 = {
	.name = "qhs_qup0",
	.id = X1E80100_SLAVE_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qup1 = {
	.name = "qhs_qup1",
	.id = X1E80100_SLAVE_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qup2 = {
	.name = "qhs_qup2",
	.id = X1E80100_SLAVE_QUP_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = X1E80100_SLAVE_SDCC_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_sdc4 = {
	.name = "qhs_sdc4",
	.id = X1E80100_SLAVE_SDCC_4,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_smmuv3_cfg = {
	.name = "qhs_smmuv3_cfg",
	.id = X1E80100_SLAVE_SMMUV3_CFG,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = X1E80100_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_tlmm = {
	.name = "qhs_tlmm",
	.id = X1E80100_SLAVE_TLMM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.id = X1E80100_SLAVE_UFS_MEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_usb2_0_cfg = {
	.name = "qhs_usb2_0_cfg",
	.id = X1E80100_SLAVE_USB2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_usb3_0_cfg = {
	.name = "qhs_usb3_0_cfg",
	.id = X1E80100_SLAVE_USB3_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_usb3_1_cfg = {
	.name = "qhs_usb3_1_cfg",
	.id = X1E80100_SLAVE_USB3_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_usb3_2_cfg = {
	.name = "qhs_usb3_2_cfg",
	.id = X1E80100_SLAVE_USB3_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_usb3_mp_cfg = {
	.name = "qhs_usb3_mp_cfg",
	.id = X1E80100_SLAVE_USB3_MP,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_usb4_0_cfg = {
	.name = "qhs_usb4_0_cfg",
	.id = X1E80100_SLAVE_USB4_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_usb4_1_cfg = {
	.name = "qhs_usb4_1_cfg",
	.id = X1E80100_SLAVE_USB4_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_usb4_2_cfg = {
	.name = "qhs_usb4_2_cfg",
	.id = X1E80100_SLAVE_USB4_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = X1E80100_SLAVE_VENUS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qss_lpass_qtb_cfg = {
	.name = "qss_lpass_qtb_cfg",
	.id = X1E80100_SLAVE_LPASS_QTB_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qss_manalc_cfg = {
	.name = "qss_manalc_cfg",
	.id = X1E80100_SLAVE_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { X1E80100_MASTER_CANALC_MANALC_CFG },
};

static struct qcom_icc_analde qss_nsp_qtb_cfg = {
	.name = "qss_nsp_qtb_cfg",
	.id = X1E80100_SLAVE_NSP_QTB_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = X1E80100_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = X1E80100_SLAVE_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_aoss = {
	.name = "qhs_aoss",
	.id = X1E80100_SLAVE_AOSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_tme_cfg = {
	.name = "qhs_tme_cfg",
	.id = X1E80100_SLAVE_TME_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_apss = {
	.name = "qns_apss",
	.id = X1E80100_SLAVE_APPSS,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde qss_cfg = {
	.name = "qss_cfg",
	.id = X1E80100_SLAVE_CANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { X1E80100_MASTER_CANALC_CFG },
};

static struct qcom_icc_analde qxs_boot_imem = {
	.name = "qxs_boot_imem",
	.id = X1E80100_SLAVE_BOOT_IMEM,
	.channels = 1,
	.buswidth = 16,
	.num_links = 0,
};

static struct qcom_icc_analde qxs_imem = {
	.name = "qxs_imem",
	.id = X1E80100_SLAVE_IMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde xs_pcie_0 = {
	.name = "xs_pcie_0",
	.id = X1E80100_SLAVE_PCIE_0,
	.channels = 1,
	.buswidth = 16,
	.num_links = 0,
};

static struct qcom_icc_analde xs_pcie_1 = {
	.name = "xs_pcie_1",
	.id = X1E80100_SLAVE_PCIE_1,
	.channels = 1,
	.buswidth = 16,
	.num_links = 0,
};

static struct qcom_icc_analde xs_pcie_2 = {
	.name = "xs_pcie_2",
	.id = X1E80100_SLAVE_PCIE_2,
	.channels = 1,
	.buswidth = 16,
	.num_links = 0,
};

static struct qcom_icc_analde xs_pcie_3 = {
	.name = "xs_pcie_3",
	.id = X1E80100_SLAVE_PCIE_3,
	.channels = 1,
	.buswidth = 64,
	.num_links = 0,
};

static struct qcom_icc_analde xs_pcie_4 = {
	.name = "xs_pcie_4",
	.id = X1E80100_SLAVE_PCIE_4,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde xs_pcie_5 = {
	.name = "xs_pcie_5",
	.id = X1E80100_SLAVE_PCIE_5,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde xs_pcie_6a = {
	.name = "xs_pcie_6a",
	.id = X1E80100_SLAVE_PCIE_6A,
	.channels = 1,
	.buswidth = 32,
	.num_links = 0,
};

static struct qcom_icc_analde xs_pcie_6b = {
	.name = "xs_pcie_6b",
	.id = X1E80100_SLAVE_PCIE_6B,
	.channels = 1,
	.buswidth = 16,
	.num_links = 0,
};

static struct qcom_icc_analde qns_gem_analc_canalc = {
	.name = "qns_gem_analc_canalc",
	.id = X1E80100_SLAVE_GEM_ANALC_CANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_MASTER_GEM_ANALC_CANALC },
};

static struct qcom_icc_analde qns_llcc = {
	.name = "qns_llcc",
	.id = X1E80100_SLAVE_LLCC,
	.channels = 8,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_MASTER_LLCC },
};

static struct qcom_icc_analde qns_pcie = {
	.name = "qns_pcie",
	.id = X1E80100_SLAVE_MEM_ANALC_PCIE_SANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { X1E80100_MASTER_GEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde qns_lpass_ag_analc_gemanalc = {
	.name = "qns_lpass_ag_analc_gemanalc",
	.id = X1E80100_SLAVE_LPASS_GEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_MASTER_LPASS_GEM_ANALC },
};

static struct qcom_icc_analde qns_lpass_agganalc = {
	.name = "qns_lpass_agganalc",
	.id = X1E80100_SLAVE_LPIAON_ANALC_LPASS_AG_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_MASTER_LPIAON_ANALC },
};

static struct qcom_icc_analde qns_lpi_aon_analc = {
	.name = "qns_lpi_aon_analc",
	.id = X1E80100_SLAVE_LPICX_ANALC_LPIAON_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_MASTER_LPASS_LPIANALC },
};

static struct qcom_icc_analde ebi = {
	.name = "ebi",
	.id = X1E80100_SLAVE_EBI1,
	.channels = 8,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_mem_analc_hf = {
	.name = "qns_mem_analc_hf",
	.id = X1E80100_SLAVE_MANALC_HF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { X1E80100_MASTER_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qns_mem_analc_sf = {
	.name = "qns_mem_analc_sf",
	.id = X1E80100_SLAVE_MANALC_SF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { X1E80100_MASTER_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde srvc_manalc = {
	.name = "srvc_manalc",
	.id = X1E80100_SLAVE_SERVICE_MANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_nsp_gemanalc = {
	.name = "qns_nsp_gemanalc",
	.id = X1E80100_SLAVE_CDSP_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { X1E80100_MASTER_COMPUTE_ANALC },
};

static struct qcom_icc_analde qns_pcie_mem_analc = {
	.name = "qns_pcie_mem_analc",
	.id = X1E80100_SLAVE_AANALC_PCIE_GEM_ANALC,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { X1E80100_MASTER_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde qns_pcie_analrth_gem_analc = {
	.name = "qns_pcie_analrth_gem_analc",
	.id = X1E80100_SLAVE_PCIE_ANALRTH,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { X1E80100_MASTER_PCIE_ANALRTH },
};

static struct qcom_icc_analde qns_pcie_south_gem_analc = {
	.name = "qns_pcie_south_gem_analc",
	.id = X1E80100_SLAVE_PCIE_SOUTH,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { X1E80100_MASTER_PCIE_SOUTH },
};

static struct qcom_icc_analde qns_gemanalc_sf = {
	.name = "qns_gemanalc_sf",
	.id = X1E80100_SLAVE_SANALC_GEM_ANALC_SF,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { X1E80100_MASTER_SANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qns_aggre_usb_sanalc = {
	.name = "qns_aggre_usb_sanalc",
	.id = X1E80100_SLAVE_USB_ANALC_SANALC,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { X1E80100_MASTER_USB_ANALC_SANALC },
};

static struct qcom_icc_analde qns_aggre_usb_analrth_sanalc = {
	.name = "qns_aggre_usb_analrth_sanalc",
	.id = X1E80100_SLAVE_AGGRE_USB_ANALRTH,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { X1E80100_MASTER_AGGRE_USB_ANALRTH },
};

static struct qcom_icc_analde qns_aggre_usb_south_sanalc = {
	.name = "qns_aggre_usb_south_sanalc",
	.id = X1E80100_SLAVE_AGGRE_USB_SOUTH,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { X1E80100_MASTER_AGGRE_USB_SOUTH },
};

static struct qcom_icc_analde qns_llcc_disp = {
	.name = "qns_llcc_disp",
	.id = X1E80100_SLAVE_LLCC_DISP,
	.channels = 8,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_MASTER_LLCC_DISP },
};

static struct qcom_icc_analde ebi_disp = {
	.name = "ebi_disp",
	.id = X1E80100_SLAVE_EBI1_DISP,
	.channels = 8,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_mem_analc_hf_disp = {
	.name = "qns_mem_analc_hf_disp",
	.id = X1E80100_SLAVE_MANALC_HF_MEM_ANALC_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { X1E80100_MASTER_MANALC_HF_MEM_ANALC_DISP },
};

static struct qcom_icc_analde qns_llcc_pcie = {
	.name = "qns_llcc_pcie",
	.id = X1E80100_SLAVE_LLCC_PCIE,
	.channels = 8,
	.buswidth = 16,
	.num_links = 1,
	.links = { X1E80100_MASTER_LLCC_PCIE },
};

static struct qcom_icc_analde ebi_pcie = {
	.name = "ebi_pcie",
	.id = X1E80100_SLAVE_EBI1_PCIE,
	.channels = 8,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_pcie_mem_analc_pcie = {
	.name = "qns_pcie_mem_analc_pcie",
	.id = X1E80100_SLAVE_AANALC_PCIE_GEM_ANALC_PCIE,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { X1E80100_MASTER_AANALC_PCIE_GEM_ANALC_PCIE },
};

static struct qcom_icc_analde qns_pcie_analrth_gem_analc_pcie = {
	.name = "qns_pcie_analrth_gem_analc_pcie",
	.id = X1E80100_SLAVE_PCIE_ANALRTH_PCIE,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { X1E80100_MASTER_PCIE_ANALRTH_PCIE },
};

static struct qcom_icc_analde qns_pcie_south_gem_analc_pcie = {
	.name = "qns_pcie_south_gem_analc_pcie",
	.id = X1E80100_SLAVE_PCIE_SOUTH_PCIE,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { X1E80100_MASTER_PCIE_SOUTH_PCIE },
};

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
	.enable_mask = BIT(3),
	.num_analdes = 1,
	.analdes = { &ebi },
};

static struct qcom_icc_bcm bcm_acv_perf = {
	.name = "ACV_PERF",
	.num_analdes = 1,
	.analdes = { &ddr_perf_mode_slave },
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.num_analdes = 1,
	.analdes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.keepalive = true,
	.num_analdes = 63,
	.analdes = { &qsm_cfg, &qhs_ahb2phy0,
		   &qhs_ahb2phy1, &qhs_ahb2phy2,
		   &qhs_av1_enc_cfg, &qhs_camera_cfg,
		   &qhs_clk_ctl, &qhs_crypto0_cfg,
		   &qhs_gpuss_cfg, &qhs_imem_cfg,
		   &qhs_ipc_router, &qhs_pcie0_cfg,
		   &qhs_pcie1_cfg, &qhs_pcie2_cfg,
		   &qhs_pcie3_cfg, &qhs_pcie4_cfg,
		   &qhs_pcie5_cfg, &qhs_pcie6a_cfg,
		   &qhs_pcie6b_cfg, &qhs_pcie_rsc_cfg,
		   &qhs_pdm, &qhs_prng,
		   &qhs_qdss_cfg, &qhs_qspi,
		   &qhs_qup0, &qhs_qup1,
		   &qhs_qup2, &qhs_sdc2,
		   &qhs_sdc4, &qhs_smmuv3_cfg,
		   &qhs_tcsr, &qhs_tlmm,
		   &qhs_ufs_mem_cfg, &qhs_usb2_0_cfg,
		   &qhs_usb3_0_cfg, &qhs_usb3_1_cfg,
		   &qhs_usb3_2_cfg, &qhs_usb3_mp_cfg,
		   &qhs_usb4_0_cfg, &qhs_usb4_1_cfg,
		   &qhs_usb4_2_cfg, &qhs_venus_cfg,
		   &qss_lpass_qtb_cfg, &qss_manalc_cfg,
		   &qss_nsp_qtb_cfg, &xs_qdss_stm,
		   &xs_sys_tcu_cfg, &qnm_gemanalc_canalc,
		   &qnm_gemanalc_pcie, &qhs_aoss,
		   &qhs_tme_cfg, &qns_apss,
		   &qss_cfg, &qxs_boot_imem,
		   &qxs_imem, &xs_pcie_0,
		   &xs_pcie_1, &xs_pcie_2,
		   &xs_pcie_3, &xs_pcie_4,
		   &xs_pcie_5, &xs_pcie_6a,
		   &xs_pcie_6b },
};

static struct qcom_icc_bcm bcm_cn1 = {
	.name = "CN1",
	.num_analdes = 1,
	.analdes = { &qhs_display_cfg },
};

static struct qcom_icc_bcm bcm_co0 = {
	.name = "CO0",
	.num_analdes = 2,
	.analdes = { &qxm_nsp, &qns_nsp_gemanalc },
};

static struct qcom_icc_bcm bcm_lp0 = {
	.name = "LP0",
	.num_analdes = 2,
	.analdes = { &qnm_lpass_lpianalc, &qns_lpass_agganalc },
};

static struct qcom_icc_bcm bcm_mc0 = {
	.name = "MC0",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &ebi },
};

static struct qcom_icc_bcm bcm_mm0 = {
	.name = "MM0",
	.num_analdes = 1,
	.analdes = { &qns_mem_analc_hf },
};

static struct qcom_icc_bcm bcm_mm1 = {
	.name = "MM1",
	.num_analdes = 10,
	.analdes = { &qnm_av1_enc, &qnm_camanalc_hf,
		   &qnm_camanalc_icp, &qnm_camanalc_sf,
		   &qnm_eva, &qnm_mdp,
		   &qnm_video, &qnm_video_cv_cpu,
		   &qnm_video_v_cpu, &qns_mem_analc_sf },
};

static struct qcom_icc_bcm bcm_pc0 = {
	.name = "PC0",
	.num_analdes = 1,
	.analdes = { &qns_pcie_mem_analc },
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.keepalive = true,
	.vote_scale = 1,
	.num_analdes = 1,
	.analdes = { &qup0_core_slave },
};

static struct qcom_icc_bcm bcm_qup1 = {
	.name = "QUP1",
	.keepalive = true,
	.vote_scale = 1,
	.num_analdes = 1,
	.analdes = { &qup1_core_slave },
};

static struct qcom_icc_bcm bcm_qup2 = {
	.name = "QUP2",
	.keepalive = true,
	.vote_scale = 1,
	.num_analdes = 1,
	.analdes = { &qup2_core_slave },
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_sh1 = {
	.name = "SH1",
	.num_analdes = 13,
	.analdes = { &alm_gpu_tcu, &alm_pcie_tcu,
		   &alm_sys_tcu, &chm_apps,
		   &qnm_gpu, &qnm_lpass,
		   &qnm_manalc_hf, &qnm_manalc_sf,
		   &qnm_nsp_analc, &qnm_pcie,
		   &xm_gic, &qns_gem_analc_canalc,
		   &qns_pcie },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &qns_gemanalc_sf },
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.num_analdes = 1,
	.analdes = { &qnm_aggre1_analc },
};

static struct qcom_icc_bcm bcm_sn3 = {
	.name = "SN3",
	.num_analdes = 1,
	.analdes = { &qnm_aggre2_analc },
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.num_analdes = 1,
	.analdes = { &qnm_usb_aanalc },
};

static struct qcom_icc_bcm bcm_acv_disp = {
	.name = "ACV",
	.num_analdes = 1,
	.analdes = { &ebi_disp },
};

static struct qcom_icc_bcm bcm_mc0_disp = {
	.name = "MC0",
	.num_analdes = 1,
	.analdes = { &ebi_disp },
};

static struct qcom_icc_bcm bcm_mm0_disp = {
	.name = "MM0",
	.num_analdes = 1,
	.analdes = { &qns_mem_analc_hf_disp },
};

static struct qcom_icc_bcm bcm_mm1_disp = {
	.name = "MM1",
	.num_analdes = 1,
	.analdes = { &qnm_mdp_disp },
};

static struct qcom_icc_bcm bcm_sh0_disp = {
	.name = "SH0",
	.num_analdes = 1,
	.analdes = { &qns_llcc_disp },
};

static struct qcom_icc_bcm bcm_sh1_disp = {
	.name = "SH1",
	.num_analdes = 2,
	.analdes = { &qnm_manalc_hf_disp, &qnm_pcie_disp },
};

static struct qcom_icc_bcm bcm_acv_pcie = {
	.name = "ACV",
	.num_analdes = 1,
	.analdes = { &ebi_pcie },
};

static struct qcom_icc_bcm bcm_mc0_pcie = {
	.name = "MC0",
	.num_analdes = 1,
	.analdes = { &ebi_pcie },
};

static struct qcom_icc_bcm bcm_pc0_pcie = {
	.name = "PC0",
	.num_analdes = 1,
	.analdes = { &qns_pcie_mem_analc_pcie },
};

static struct qcom_icc_bcm bcm_sh0_pcie = {
	.name = "SH0",
	.num_analdes = 1,
	.analdes = { &qns_llcc_pcie },
};

static struct qcom_icc_bcm bcm_sh1_pcie = {
	.name = "SH1",
	.num_analdes = 1,
	.analdes = { &qnm_pcie_pcie },
};

static struct qcom_icc_bcm *aggre1_analc_bcms[] = {
};

static struct qcom_icc_analde * const aggre1_analc_analdes[] = {
	[MASTER_QSPI_0] = &qhm_qspi,
	[MASTER_QUP_1] = &qhm_qup1,
	[MASTER_SDCC_4] = &xm_sdc4,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[SLAVE_A1ANALC_SANALC] = &qns_a1analc_sanalc,
};

static const struct qcom_icc_desc x1e80100_aggre1_analc = {
	.analdes = aggre1_analc_analdes,
	.num_analdes = ARRAY_SIZE(aggre1_analc_analdes),
	.bcms = aggre1_analc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_analc_bcms),
};

static struct qcom_icc_bcm * const aggre2_analc_bcms[] = {
	&bcm_ce0,
};

static struct qcom_icc_analde * const aggre2_analc_analdes[] = {
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_QUP_2] = &qhm_qup2,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_SP] = &qxm_sp,
	[MASTER_QDSS_ETR] = &xm_qdss_etr_0,
	[MASTER_QDSS_ETR_1] = &xm_qdss_etr_1,
	[MASTER_SDCC_2] = &xm_sdc2,
	[SLAVE_A2ANALC_SANALC] = &qns_a2analc_sanalc,
};

static const struct qcom_icc_desc x1e80100_aggre2_analc = {
	.analdes = aggre2_analc_analdes,
	.num_analdes = ARRAY_SIZE(aggre2_analc_analdes),
	.bcms = aggre2_analc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_analc_bcms),
};

static struct qcom_icc_bcm * const clk_virt_bcms[] = {
	&bcm_acv_perf,
	&bcm_qup0,
	&bcm_qup1,
	&bcm_qup2,
};

static struct qcom_icc_analde * const clk_virt_analdes[] = {
	[MASTER_DDR_PERF_MODE] = &ddr_perf_mode_master,
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[MASTER_QUP_CORE_1] = &qup1_core_master,
	[MASTER_QUP_CORE_2] = &qup2_core_master,
	[SLAVE_DDR_PERF_MODE] = &ddr_perf_mode_slave,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[SLAVE_QUP_CORE_1] = &qup1_core_slave,
	[SLAVE_QUP_CORE_2] = &qup2_core_slave,
};

static const struct qcom_icc_desc x1e80100_clk_virt = {
	.analdes = clk_virt_analdes,
	.num_analdes = ARRAY_SIZE(clk_virt_analdes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
};

static struct qcom_icc_bcm * const canalc_cfg_bcms[] = {
	&bcm_cn0,
	&bcm_cn1,
};

static struct qcom_icc_analde * const canalc_cfg_analdes[] = {
	[MASTER_CANALC_CFG] = &qsm_cfg,
	[SLAVE_AHB2PHY_SOUTH] = &qhs_ahb2phy0,
	[SLAVE_AHB2PHY_ANALRTH] = &qhs_ahb2phy1,
	[SLAVE_AHB2PHY_2] = &qhs_ahb2phy2,
	[SLAVE_AV1_ENC_CFG] = &qhs_av1_enc_cfg,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_GFX3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPC_ROUTER_CFG] = &qhs_ipc_router,
	[SLAVE_PCIE_0_CFG] = &qhs_pcie0_cfg,
	[SLAVE_PCIE_1_CFG] = &qhs_pcie1_cfg,
	[SLAVE_PCIE_2_CFG] = &qhs_pcie2_cfg,
	[SLAVE_PCIE_3_CFG] = &qhs_pcie3_cfg,
	[SLAVE_PCIE_4_CFG] = &qhs_pcie4_cfg,
	[SLAVE_PCIE_5_CFG] = &qhs_pcie5_cfg,
	[SLAVE_PCIE_6A_CFG] = &qhs_pcie6a_cfg,
	[SLAVE_PCIE_6B_CFG] = &qhs_pcie6b_cfg,
	[SLAVE_PCIE_RSC_CFG] = &qhs_pcie_rsc_cfg,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QSPI_0] = &qhs_qspi,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_QUP_2] = &qhs_qup2,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SDCC_4] = &qhs_sdc4,
	[SLAVE_SMMUV3_CFG] = &qhs_smmuv3_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB2] = &qhs_usb2_0_cfg,
	[SLAVE_USB3_0] = &qhs_usb3_0_cfg,
	[SLAVE_USB3_1] = &qhs_usb3_1_cfg,
	[SLAVE_USB3_2] = &qhs_usb3_2_cfg,
	[SLAVE_USB3_MP] = &qhs_usb3_mp_cfg,
	[SLAVE_USB4_0] = &qhs_usb4_0_cfg,
	[SLAVE_USB4_1] = &qhs_usb4_1_cfg,
	[SLAVE_USB4_2] = &qhs_usb4_2_cfg,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_LPASS_QTB_CFG] = &qss_lpass_qtb_cfg,
	[SLAVE_CANALC_MANALC_CFG] = &qss_manalc_cfg,
	[SLAVE_NSP_QTB_CFG] = &qss_nsp_qtb_cfg,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc x1e80100_canalc_cfg = {
	.analdes = canalc_cfg_analdes,
	.num_analdes = ARRAY_SIZE(canalc_cfg_analdes),
	.bcms = canalc_cfg_bcms,
	.num_bcms = ARRAY_SIZE(canalc_cfg_bcms),
};

static struct qcom_icc_bcm * const canalc_main_bcms[] = {
	&bcm_cn0,
};

static struct qcom_icc_analde * const canalc_main_analdes[] = {
	[MASTER_GEM_ANALC_CANALC] = &qnm_gemanalc_canalc,
	[MASTER_GEM_ANALC_PCIE_SANALC] = &qnm_gemanalc_pcie,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_TME_CFG] = &qhs_tme_cfg,
	[SLAVE_APPSS] = &qns_apss,
	[SLAVE_CANALC_CFG] = &qss_cfg,
	[SLAVE_BOOT_IMEM] = &qxs_boot_imem,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_PCIE_0] = &xs_pcie_0,
	[SLAVE_PCIE_1] = &xs_pcie_1,
	[SLAVE_PCIE_2] = &xs_pcie_2,
	[SLAVE_PCIE_3] = &xs_pcie_3,
	[SLAVE_PCIE_4] = &xs_pcie_4,
	[SLAVE_PCIE_5] = &xs_pcie_5,
	[SLAVE_PCIE_6A] = &xs_pcie_6a,
	[SLAVE_PCIE_6B] = &xs_pcie_6b,
};

static const struct qcom_icc_desc x1e80100_canalc_main = {
	.analdes = canalc_main_analdes,
	.num_analdes = ARRAY_SIZE(canalc_main_analdes),
	.bcms = canalc_main_bcms,
	.num_bcms = ARRAY_SIZE(canalc_main_bcms),
};

static struct qcom_icc_bcm * const gem_analc_bcms[] = {
	&bcm_sh0,
	&bcm_sh1,
	&bcm_sh0_disp,
	&bcm_sh1_disp,
	&bcm_sh0_pcie,
	&bcm_sh1_pcie,
};

static struct qcom_icc_analde * const gem_analc_analdes[] = {
	[MASTER_GPU_TCU] = &alm_gpu_tcu,
	[MASTER_PCIE_TCU] = &alm_pcie_tcu,
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_APPSS_PROC] = &chm_apps,
	[MASTER_GFX3D] = &qnm_gpu,
	[MASTER_LPASS_GEM_ANALC] = &qnm_lpass,
	[MASTER_MANALC_HF_MEM_ANALC] = &qnm_manalc_hf,
	[MASTER_MANALC_SF_MEM_ANALC] = &qnm_manalc_sf,
	[MASTER_COMPUTE_ANALC] = &qnm_nsp_analc,
	[MASTER_AANALC_PCIE_GEM_ANALC] = &qnm_pcie,
	[MASTER_SANALC_SF_MEM_ANALC] = &qnm_sanalc_sf,
	[MASTER_GIC2] = &xm_gic,
	[SLAVE_GEM_ANALC_CANALC] = &qns_gem_analc_canalc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_MEM_ANALC_PCIE_SANALC] = &qns_pcie,
	[MASTER_MANALC_HF_MEM_ANALC_DISP] = &qnm_manalc_hf_disp,
	[MASTER_AANALC_PCIE_GEM_ANALC_DISP] = &qnm_pcie_disp,
	[SLAVE_LLCC_DISP] = &qns_llcc_disp,
	[MASTER_AANALC_PCIE_GEM_ANALC_PCIE] = &qnm_pcie_pcie,
	[SLAVE_LLCC_PCIE] = &qns_llcc_pcie,
};

static const struct qcom_icc_desc x1e80100_gem_analc = {
	.analdes = gem_analc_analdes,
	.num_analdes = ARRAY_SIZE(gem_analc_analdes),
	.bcms = gem_analc_bcms,
	.num_bcms = ARRAY_SIZE(gem_analc_bcms),
};

static struct qcom_icc_bcm *lpass_ag_analc_bcms[] = {
};

static struct qcom_icc_analde * const lpass_ag_analc_analdes[] = {
	[MASTER_LPIAON_ANALC] = &qnm_lpiaon_analc,
	[SLAVE_LPASS_GEM_ANALC] = &qns_lpass_ag_analc_gemanalc,
};

static const struct qcom_icc_desc x1e80100_lpass_ag_analc = {
	.analdes = lpass_ag_analc_analdes,
	.num_analdes = ARRAY_SIZE(lpass_ag_analc_analdes),
	.bcms = lpass_ag_analc_bcms,
	.num_bcms = ARRAY_SIZE(lpass_ag_analc_bcms),
};

static struct qcom_icc_bcm * const lpass_lpiaon_analc_bcms[] = {
	&bcm_lp0,
};

static struct qcom_icc_analde * const lpass_lpiaon_analc_analdes[] = {
	[MASTER_LPASS_LPIANALC] = &qnm_lpass_lpianalc,
	[SLAVE_LPIAON_ANALC_LPASS_AG_ANALC] = &qns_lpass_agganalc,
};

static const struct qcom_icc_desc x1e80100_lpass_lpiaon_analc = {
	.analdes = lpass_lpiaon_analc_analdes,
	.num_analdes = ARRAY_SIZE(lpass_lpiaon_analc_analdes),
	.bcms = lpass_lpiaon_analc_bcms,
	.num_bcms = ARRAY_SIZE(lpass_lpiaon_analc_bcms),
};

static struct qcom_icc_bcm * const lpass_lpicx_analc_bcms[] = {
};

static struct qcom_icc_analde * const lpass_lpicx_analc_analdes[] = {
	[MASTER_LPASS_PROC] = &qxm_lpianalc_dsp_axim,
	[SLAVE_LPICX_ANALC_LPIAON_ANALC] = &qns_lpi_aon_analc,
};

static const struct qcom_icc_desc x1e80100_lpass_lpicx_analc = {
	.analdes = lpass_lpicx_analc_analdes,
	.num_analdes = ARRAY_SIZE(lpass_lpicx_analc_analdes),
	.bcms = lpass_lpicx_analc_bcms,
	.num_bcms = ARRAY_SIZE(lpass_lpicx_analc_bcms),
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
	&bcm_acv_disp,
	&bcm_mc0_disp,
	&bcm_acv_pcie,
	&bcm_mc0_pcie,
};

static struct qcom_icc_analde * const mc_virt_analdes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
	[MASTER_LLCC_DISP] = &llcc_mc_disp,
	[SLAVE_EBI1_DISP] = &ebi_disp,
	[MASTER_LLCC_PCIE] = &llcc_mc_pcie,
	[SLAVE_EBI1_PCIE] = &ebi_pcie,
};

static const struct qcom_icc_desc x1e80100_mc_virt = {
	.analdes = mc_virt_analdes,
	.num_analdes = ARRAY_SIZE(mc_virt_analdes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm * const mmss_analc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
	&bcm_mm0_disp,
	&bcm_mm1_disp,
};

static struct qcom_icc_analde * const mmss_analc_analdes[] = {
	[MASTER_AV1_ENC] = &qnm_av1_enc,
	[MASTER_CAMANALC_HF] = &qnm_camanalc_hf,
	[MASTER_CAMANALC_ICP] = &qnm_camanalc_icp,
	[MASTER_CAMANALC_SF] = &qnm_camanalc_sf,
	[MASTER_EVA] = &qnm_eva,
	[MASTER_MDP] = &qnm_mdp,
	[MASTER_VIDEO] = &qnm_video,
	[MASTER_VIDEO_CV_PROC] = &qnm_video_cv_cpu,
	[MASTER_VIDEO_V_PROC] = &qnm_video_v_cpu,
	[MASTER_CANALC_MANALC_CFG] = &qsm_manalc_cfg,
	[SLAVE_MANALC_HF_MEM_ANALC] = &qns_mem_analc_hf,
	[SLAVE_MANALC_SF_MEM_ANALC] = &qns_mem_analc_sf,
	[SLAVE_SERVICE_MANALC] = &srvc_manalc,
	[MASTER_MDP_DISP] = &qnm_mdp_disp,
	[SLAVE_MANALC_HF_MEM_ANALC_DISP] = &qns_mem_analc_hf_disp,
};

static const struct qcom_icc_desc x1e80100_mmss_analc = {
	.analdes = mmss_analc_analdes,
	.num_analdes = ARRAY_SIZE(mmss_analc_analdes),
	.bcms = mmss_analc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_analc_bcms),
};

static struct qcom_icc_bcm * const nsp_analc_bcms[] = {
	&bcm_co0,
};

static struct qcom_icc_analde * const nsp_analc_analdes[] = {
	[MASTER_CDSP_PROC] = &qxm_nsp,
	[SLAVE_CDSP_MEM_ANALC] = &qns_nsp_gemanalc,
};

static const struct qcom_icc_desc x1e80100_nsp_analc = {
	.analdes = nsp_analc_analdes,
	.num_analdes = ARRAY_SIZE(nsp_analc_analdes),
	.bcms = nsp_analc_bcms,
	.num_bcms = ARRAY_SIZE(nsp_analc_bcms),
};

static struct qcom_icc_bcm * const pcie_center_aanalc_bcms[] = {
	&bcm_pc0,
	&bcm_pc0_pcie,
};

static struct qcom_icc_analde * const pcie_center_aanalc_analdes[] = {
	[MASTER_PCIE_ANALRTH] = &qnm_pcie_analrth_gem_analc,
	[MASTER_PCIE_SOUTH] = &qnm_pcie_south_gem_analc,
	[SLAVE_AANALC_PCIE_GEM_ANALC] = &qns_pcie_mem_analc,
	[MASTER_PCIE_ANALRTH_PCIE] = &qnm_pcie_analrth_gem_analc_pcie,
	[MASTER_PCIE_SOUTH_PCIE] = &qnm_pcie_south_gem_analc_pcie,
	[SLAVE_AANALC_PCIE_GEM_ANALC_PCIE] = &qns_pcie_mem_analc_pcie,
};

static const struct qcom_icc_desc x1e80100_pcie_center_aanalc = {
	.analdes = pcie_center_aanalc_analdes,
	.num_analdes = ARRAY_SIZE(pcie_center_aanalc_analdes),
	.bcms = pcie_center_aanalc_bcms,
	.num_bcms = ARRAY_SIZE(pcie_center_aanalc_bcms),
};

static struct qcom_icc_bcm * const pcie_analrth_aanalc_bcms[] = {
};

static struct qcom_icc_analde * const pcie_analrth_aanalc_analdes[] = {
	[MASTER_PCIE_3] = &xm_pcie_3,
	[MASTER_PCIE_4] = &xm_pcie_4,
	[MASTER_PCIE_5] = &xm_pcie_5,
	[SLAVE_PCIE_ANALRTH] = &qns_pcie_analrth_gem_analc,
	[MASTER_PCIE_3_PCIE] = &xm_pcie_3_pcie,
	[MASTER_PCIE_4_PCIE] = &xm_pcie_4_pcie,
	[MASTER_PCIE_5_PCIE] = &xm_pcie_5_pcie,
	[SLAVE_PCIE_ANALRTH_PCIE] = &qns_pcie_analrth_gem_analc_pcie,
};

static const struct qcom_icc_desc x1e80100_pcie_analrth_aanalc = {
	.analdes = pcie_analrth_aanalc_analdes,
	.num_analdes = ARRAY_SIZE(pcie_analrth_aanalc_analdes),
	.bcms = pcie_analrth_aanalc_bcms,
	.num_bcms = ARRAY_SIZE(pcie_analrth_aanalc_bcms),
};

static struct qcom_icc_bcm *pcie_south_aanalc_bcms[] = {
};

static struct qcom_icc_analde * const pcie_south_aanalc_analdes[] = {
	[MASTER_PCIE_0] = &xm_pcie_0,
	[MASTER_PCIE_1] = &xm_pcie_1,
	[MASTER_PCIE_2] = &xm_pcie_2,
	[MASTER_PCIE_6A] = &xm_pcie_6a,
	[MASTER_PCIE_6B] = &xm_pcie_6b,
	[SLAVE_PCIE_SOUTH] = &qns_pcie_south_gem_analc,
	[MASTER_PCIE_0_PCIE] = &xm_pcie_0_pcie,
	[MASTER_PCIE_1_PCIE] = &xm_pcie_1_pcie,
	[MASTER_PCIE_2_PCIE] = &xm_pcie_2_pcie,
	[MASTER_PCIE_6A_PCIE] = &xm_pcie_6a_pcie,
	[MASTER_PCIE_6B_PCIE] = &xm_pcie_6b_pcie,
	[SLAVE_PCIE_SOUTH_PCIE] = &qns_pcie_south_gem_analc_pcie,
};

static const struct qcom_icc_desc x1e80100_pcie_south_aanalc = {
	.analdes = pcie_south_aanalc_analdes,
	.num_analdes = ARRAY_SIZE(pcie_south_aanalc_analdes),
	.bcms = pcie_south_aanalc_bcms,
	.num_bcms = ARRAY_SIZE(pcie_south_aanalc_bcms),
};

static struct qcom_icc_bcm *system_analc_bcms[] = {
	&bcm_sn0,
	&bcm_sn2,
	&bcm_sn3,
	&bcm_sn4,
};

static struct qcom_icc_analde * const system_analc_analdes[] = {
	[MASTER_A1ANALC_SANALC] = &qnm_aggre1_analc,
	[MASTER_A2ANALC_SANALC] = &qnm_aggre2_analc,
	[MASTER_GIC1] = &qnm_gic,
	[MASTER_USB_ANALC_SANALC] = &qnm_usb_aanalc,
	[SLAVE_SANALC_GEM_ANALC_SF] = &qns_gemanalc_sf,
};

static const struct qcom_icc_desc x1e80100_system_analc = {
	.analdes = system_analc_analdes,
	.num_analdes = ARRAY_SIZE(system_analc_analdes),
	.bcms = system_analc_bcms,
	.num_bcms = ARRAY_SIZE(system_analc_bcms),
};

static struct qcom_icc_bcm * const usb_center_aanalc_bcms[] = {
};

static struct qcom_icc_analde * const usb_center_aanalc_analdes[] = {
	[MASTER_AGGRE_USB_ANALRTH] = &qnm_aggre_usb_analrth_sanalc,
	[MASTER_AGGRE_USB_SOUTH] = &qnm_aggre_usb_south_sanalc,
	[SLAVE_USB_ANALC_SANALC] = &qns_aggre_usb_sanalc,
};

static const struct qcom_icc_desc x1e80100_usb_center_aanalc = {
	.analdes = usb_center_aanalc_analdes,
	.num_analdes = ARRAY_SIZE(usb_center_aanalc_analdes),
	.bcms = usb_center_aanalc_bcms,
	.num_bcms = ARRAY_SIZE(usb_center_aanalc_bcms),
};

static struct qcom_icc_bcm *usb_analrth_aanalc_bcms[] = {
};

static struct qcom_icc_analde * const usb_analrth_aanalc_analdes[] = {
	[MASTER_USB2] = &xm_usb2_0,
	[MASTER_USB3_MP] = &xm_usb3_mp,
	[SLAVE_AGGRE_USB_ANALRTH] = &qns_aggre_usb_analrth_sanalc,
};

static const struct qcom_icc_desc x1e80100_usb_analrth_aanalc = {
	.analdes = usb_analrth_aanalc_analdes,
	.num_analdes = ARRAY_SIZE(usb_analrth_aanalc_analdes),
	.bcms = usb_analrth_aanalc_bcms,
	.num_bcms = ARRAY_SIZE(usb_analrth_aanalc_bcms),
};

static struct qcom_icc_bcm *usb_south_aanalc_bcms[] = {
};

static struct qcom_icc_analde * const usb_south_aanalc_analdes[] = {
	[MASTER_USB3_0] = &xm_usb3_0,
	[MASTER_USB3_1] = &xm_usb3_1,
	[MASTER_USB3_2] = &xm_usb3_2,
	[MASTER_USB4_0] = &xm_usb4_0,
	[MASTER_USB4_1] = &xm_usb4_1,
	[MASTER_USB4_2] = &xm_usb4_2,
	[SLAVE_AGGRE_USB_SOUTH] = &qns_aggre_usb_south_sanalc,
};

static const struct qcom_icc_desc x1e80100_usb_south_aanalc = {
	.analdes = usb_south_aanalc_analdes,
	.num_analdes = ARRAY_SIZE(usb_south_aanalc_analdes),
	.bcms = usb_south_aanalc_bcms,
	.num_bcms = ARRAY_SIZE(usb_south_aanalc_bcms),
};

static const struct of_device_id qanalc_of_match[] = {
	{ .compatible = "qcom,x1e80100-aggre1-analc", .data = &x1e80100_aggre1_analc},
	{ .compatible = "qcom,x1e80100-aggre2-analc", .data = &x1e80100_aggre2_analc},
	{ .compatible = "qcom,x1e80100-clk-virt", .data = &x1e80100_clk_virt},
	{ .compatible = "qcom,x1e80100-canalc-cfg", .data = &x1e80100_canalc_cfg},
	{ .compatible = "qcom,x1e80100-canalc-main", .data = &x1e80100_canalc_main},
	{ .compatible = "qcom,x1e80100-gem-analc", .data = &x1e80100_gem_analc},
	{ .compatible = "qcom,x1e80100-lpass-ag-analc", .data = &x1e80100_lpass_ag_analc},
	{ .compatible = "qcom,x1e80100-lpass-lpiaon-analc", .data = &x1e80100_lpass_lpiaon_analc},
	{ .compatible = "qcom,x1e80100-lpass-lpicx-analc", .data = &x1e80100_lpass_lpicx_analc},
	{ .compatible = "qcom,x1e80100-mc-virt", .data = &x1e80100_mc_virt},
	{ .compatible = "qcom,x1e80100-mmss-analc", .data = &x1e80100_mmss_analc},
	{ .compatible = "qcom,x1e80100-nsp-analc", .data = &x1e80100_nsp_analc},
	{ .compatible = "qcom,x1e80100-pcie-center-aanalc", .data = &x1e80100_pcie_center_aanalc},
	{ .compatible = "qcom,x1e80100-pcie-analrth-aanalc", .data = &x1e80100_pcie_analrth_aanalc},
	{ .compatible = "qcom,x1e80100-pcie-south-aanalc", .data = &x1e80100_pcie_south_aanalc},
	{ .compatible = "qcom,x1e80100-system-analc", .data = &x1e80100_system_analc},
	{ .compatible = "qcom,x1e80100-usb-center-aanalc", .data = &x1e80100_usb_center_aanalc},
	{ .compatible = "qcom,x1e80100-usb-analrth-aanalc", .data = &x1e80100_usb_analrth_aanalc},
	{ .compatible = "qcom,x1e80100-usb-south-aanalc", .data = &x1e80100_usb_south_aanalc},
	{ }
};
MODULE_DEVICE_TABLE(of, qanalc_of_match);

static struct platform_driver qanalc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove_new = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qanalc-x1e80100",
		.of_match_table = qanalc_of_match,
		.sync_state = icc_sync_state,
	},
};

static int __init qanalc_driver_init(void)
{
	return platform_driver_register(&qanalc_driver);
}
core_initcall(qanalc_driver_init);

static void __exit qanalc_driver_exit(void)
{
	platform_driver_unregister(&qanalc_driver);
}
module_exit(qanalc_driver_exit);

MODULE_DESCRIPTION("x1e80100 AnalC driver");
MODULE_LICENSE("GPL");
