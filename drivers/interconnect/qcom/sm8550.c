// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Inanalvation Center, Inc. All rights reserved.
 * Copyright (c) 2022, Linaro Limited
 *
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <dt-bindings/interconnect/qcom,sm8550-rpmh.h>

#include "bcm-voter.h"
#include "icc-common.h"
#include "icc-rpmh.h"
#include "sm8550.h"

static struct qcom_icc_analde qhm_qspi = {
	.name = "qhm_qspi",
	.id = SM8550_MASTER_QSPI_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8550_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qup1 = {
	.name = "qhm_qup1",
	.id = SM8550_MASTER_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8550_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_sdc4 = {
	.name = "xm_sdc4",
	.id = SM8550_MASTER_SDCC_4,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8550_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.id = SM8550_MASTER_UFS_MEM,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_usb3_0 = {
	.name = "xm_usb3_0",
	.id = SM8550_MASTER_USB3_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8550_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = SM8550_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8550_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qup2 = {
	.name = "qhm_qup2",
	.id = SM8550_MASTER_QUP_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8550_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qxm_crypto = {
	.name = "qxm_crypto",
	.id = SM8550_MASTER_CRYPTO,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8550_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qxm_ipa = {
	.name = "qxm_ipa",
	.id = SM8550_MASTER_IPA,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8550_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qxm_sp = {
	.name = "qxm_sp",
	.id = SM8550_MASTER_SP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8550_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde xm_qdss_etr_0 = {
	.name = "xm_qdss_etr_0",
	.id = SM8550_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8550_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde xm_qdss_etr_1 = {
	.name = "xm_qdss_etr_1",
	.id = SM8550_MASTER_QDSS_ETR_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8550_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde xm_sdc2 = {
	.name = "xm_sdc2",
	.id = SM8550_MASTER_SDCC_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8550_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qup0_core_master = {
	.name = "qup0_core_master",
	.id = SM8550_MASTER_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8550_SLAVE_QUP_CORE_0 },
};

static struct qcom_icc_analde qup1_core_master = {
	.name = "qup1_core_master",
	.id = SM8550_MASTER_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8550_SLAVE_QUP_CORE_1 },
};

static struct qcom_icc_analde qup2_core_master = {
	.name = "qup2_core_master",
	.id = SM8550_MASTER_QUP_CORE_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8550_SLAVE_QUP_CORE_2 },
};

static struct qcom_icc_analde qsm_cfg = {
	.name = "qsm_cfg",
	.id = SM8550_MASTER_CANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 44,
	.links = { SM8550_SLAVE_AHB2PHY_SOUTH, SM8550_SLAVE_AHB2PHY_ANALRTH,
		   SM8550_SLAVE_APPSS, SM8550_SLAVE_CAMERA_CFG,
		   SM8550_SLAVE_CLK_CTL, SM8550_SLAVE_RBCPR_CX_CFG,
		   SM8550_SLAVE_RBCPR_MMCX_CFG, SM8550_SLAVE_RBCPR_MXA_CFG,
		   SM8550_SLAVE_RBCPR_MXC_CFG, SM8550_SLAVE_CPR_NSPCX,
		   SM8550_SLAVE_CRYPTO_0_CFG, SM8550_SLAVE_CX_RDPM,
		   SM8550_SLAVE_DISPLAY_CFG, SM8550_SLAVE_GFX3D_CFG,
		   SM8550_SLAVE_I2C, SM8550_SLAVE_IMEM_CFG,
		   SM8550_SLAVE_IPA_CFG, SM8550_SLAVE_IPC_ROUTER_CFG,
		   SM8550_SLAVE_CANALC_MSS, SM8550_SLAVE_MX_RDPM,
		   SM8550_SLAVE_PCIE_0_CFG, SM8550_SLAVE_PCIE_1_CFG,
		   SM8550_SLAVE_PDM, SM8550_SLAVE_PIMEM_CFG,
		   SM8550_SLAVE_PRNG, SM8550_SLAVE_QDSS_CFG,
		   SM8550_SLAVE_QSPI_0, SM8550_SLAVE_QUP_1,
		   SM8550_SLAVE_QUP_2, SM8550_SLAVE_SDCC_2,
		   SM8550_SLAVE_SDCC_4, SM8550_SLAVE_SPSS_CFG,
		   SM8550_SLAVE_TCSR, SM8550_SLAVE_TLMM,
		   SM8550_SLAVE_UFS_MEM_CFG, SM8550_SLAVE_USB3_0,
		   SM8550_SLAVE_VENUS_CFG, SM8550_SLAVE_VSENSE_CTRL_CFG,
		   SM8550_SLAVE_LPASS_QTB_CFG, SM8550_SLAVE_CANALC_MANALC_CFG,
		   SM8550_SLAVE_NSP_QTB_CFG, SM8550_SLAVE_PCIE_AANALC_CFG,
		   SM8550_SLAVE_QDSS_STM, SM8550_SLAVE_TCU },
};

static struct qcom_icc_analde qnm_gemanalc_canalc = {
	.name = "qnm_gemanalc_canalc",
	.id = SM8550_MASTER_GEM_ANALC_CANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 6,
	.links = { SM8550_SLAVE_AOSS, SM8550_SLAVE_TME_CFG,
		   SM8550_SLAVE_CANALC_CFG, SM8550_SLAVE_DDRSS_CFG,
		   SM8550_SLAVE_BOOT_IMEM, SM8550_SLAVE_IMEM },
};

static struct qcom_icc_analde qnm_gemanalc_pcie = {
	.name = "qnm_gemanalc_pcie",
	.id = SM8550_MASTER_GEM_ANALC_PCIE_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM8550_SLAVE_PCIE_0, SM8550_SLAVE_PCIE_1 },
};

static struct qcom_icc_analde alm_gpu_tcu = {
	.name = "alm_gpu_tcu",
	.id = SM8550_MASTER_GPU_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM8550_SLAVE_GEM_ANALC_CANALC, SM8550_SLAVE_LLCC },
};

static struct qcom_icc_analde alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.id = SM8550_MASTER_SYS_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM8550_SLAVE_GEM_ANALC_CANALC, SM8550_SLAVE_LLCC },
};

static struct qcom_icc_analde chm_apps = {
	.name = "chm_apps",
	.id = SM8550_MASTER_APPSS_PROC,
	.channels = 3,
	.buswidth = 32,
	.num_links = 3,
	.links = { SM8550_SLAVE_GEM_ANALC_CANALC, SM8550_SLAVE_LLCC,
		   SM8550_SLAVE_MEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde qnm_gpu = {
	.name = "qnm_gpu",
	.id = SM8550_MASTER_GFX3D,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM8550_SLAVE_GEM_ANALC_CANALC, SM8550_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_lpass_gemanalc = {
	.name = "qnm_lpass_gemanalc",
	.id = SM8550_MASTER_LPASS_GEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { SM8550_SLAVE_GEM_ANALC_CANALC, SM8550_SLAVE_LLCC,
		   SM8550_SLAVE_MEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde qnm_mdsp = {
	.name = "qnm_mdsp",
	.id = SM8550_MASTER_MSS_PROC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { SM8550_SLAVE_GEM_ANALC_CANALC, SM8550_SLAVE_LLCC,
		   SM8550_SLAVE_MEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde qnm_manalc_hf = {
	.name = "qnm_manalc_hf",
	.id = SM8550_MASTER_MANALC_HF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM8550_SLAVE_GEM_ANALC_CANALC, SM8550_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_manalc_sf = {
	.name = "qnm_manalc_sf",
	.id = SM8550_MASTER_MANALC_SF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM8550_SLAVE_GEM_ANALC_CANALC, SM8550_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_nsp_gemanalc = {
	.name = "qnm_nsp_gemanalc",
	.id = SM8550_MASTER_COMPUTE_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM8550_SLAVE_GEM_ANALC_CANALC, SM8550_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_pcie = {
	.name = "qnm_pcie",
	.id = SM8550_MASTER_AANALC_PCIE_GEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 2,
	.links = { SM8550_SLAVE_GEM_ANALC_CANALC, SM8550_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_sanalc_gc = {
	.name = "qnm_sanalc_gc",
	.id = SM8550_MASTER_SANALC_GC_MEM_ANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8550_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_sanalc_sf = {
	.name = "qnm_sanalc_sf",
	.id = SM8550_MASTER_SANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { SM8550_SLAVE_GEM_ANALC_CANALC, SM8550_SLAVE_LLCC,
		   SM8550_SLAVE_MEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde qnm_lpiaon_analc = {
	.name = "qnm_lpiaon_analc",
	.id = SM8550_MASTER_LPIAON_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_SLAVE_LPASS_GEM_ANALC },
};

static struct qcom_icc_analde qnm_lpass_lpianalc = {
	.name = "qnm_lpass_lpianalc",
	.id = SM8550_MASTER_LPASS_LPIANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_SLAVE_LPIAON_ANALC_LPASS_AG_ANALC },
};

static struct qcom_icc_analde qxm_lpianalc_dsp_axim = {
	.name = "qxm_lpianalc_dsp_axim",
	.id = SM8550_MASTER_LPASS_PROC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_SLAVE_LPICX_ANALC_LPIAON_ANALC },
};

static struct qcom_icc_analde llcc_mc = {
	.name = "llcc_mc",
	.id = SM8550_MASTER_LLCC,
	.channels = 4,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8550_SLAVE_EBI1 },
};

static struct qcom_icc_analde qnm_camanalc_hf = {
	.name = "qnm_camanalc_hf",
	.id = SM8550_MASTER_CAMANALC_HF,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_camanalc_icp = {
	.name = "qnm_camanalc_icp",
	.id = SM8550_MASTER_CAMANALC_ICP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8550_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_camanalc_sf = {
	.name = "qnm_camanalc_sf",
	.id = SM8550_MASTER_CAMANALC_SF,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_mdp = {
	.name = "qnm_mdp",
	.id = SM8550_MASTER_MDP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_vapss_hcp = {
	.name = "qnm_vapss_hcp",
	.id = SM8550_MASTER_CDSP_HCP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_video = {
	.name = "qnm_video",
	.id = SM8550_MASTER_VIDEO,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_video_cv_cpu = {
	.name = "qnm_video_cv_cpu",
	.id = SM8550_MASTER_VIDEO_CV_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8550_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_video_cvp = {
	.name = "qnm_video_cvp",
	.id = SM8550_MASTER_VIDEO_PROC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_video_v_cpu = {
	.name = "qnm_video_v_cpu",
	.id = SM8550_MASTER_VIDEO_V_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8550_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qsm_manalc_cfg = {
	.name = "qsm_manalc_cfg",
	.id = SM8550_MASTER_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8550_SLAVE_SERVICE_MANALC },
};

static struct qcom_icc_analde qxm_nsp = {
	.name = "qxm_nsp",
	.id = SM8550_MASTER_CDSP_PROC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_SLAVE_CDSP_MEM_ANALC },
};

static struct qcom_icc_analde qsm_pcie_aanalc_cfg = {
	.name = "qsm_pcie_aanalc_cfg",
	.id = SM8550_MASTER_PCIE_AANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8550_SLAVE_SERVICE_PCIE_AANALC },
};

static struct qcom_icc_analde xm_pcie3_0 = {
	.name = "xm_pcie3_0",
	.id = SM8550_MASTER_PCIE_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8550_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde xm_pcie3_1 = {
	.name = "xm_pcie3_1",
	.id = SM8550_MASTER_PCIE_1,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde qhm_gic = {
	.name = "qhm_gic",
	.id = SM8550_MASTER_GIC_AHB,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8550_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qnm_aggre1_analc = {
	.name = "qnm_aggre1_analc",
	.id = SM8550_MASTER_A1ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qnm_aggre2_analc = {
	.name = "qnm_aggre2_analc",
	.id = SM8550_MASTER_A2ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde xm_gic = {
	.name = "xm_gic",
	.id = SM8550_MASTER_GIC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8550_SLAVE_SANALC_GEM_ANALC_GC },
};

static struct qcom_icc_analde qnm_manalc_hf_disp = {
	.name = "qnm_manalc_hf_disp",
	.id = SM8550_MASTER_MANALC_HF_MEM_ANALC_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_SLAVE_LLCC_DISP },
};

static struct qcom_icc_analde qnm_pcie_disp = {
	.name = "qnm_pcie_disp",
	.id = SM8550_MASTER_AANALC_PCIE_GEM_ANALC_DISP,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_SLAVE_LLCC_DISP },
};

static struct qcom_icc_analde llcc_mc_disp = {
	.name = "llcc_mc_disp",
	.id = SM8550_MASTER_LLCC_DISP,
	.channels = 4,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8550_SLAVE_EBI1_DISP },
};

static struct qcom_icc_analde qnm_mdp_disp = {
	.name = "qnm_mdp_disp",
	.id = SM8550_MASTER_MDP_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_SLAVE_MANALC_HF_MEM_ANALC_DISP },
};

static struct qcom_icc_analde qnm_manalc_hf_cam_ife_0 = {
	.name = "qnm_manalc_hf_cam_ife_0",
	.id = SM8550_MASTER_MANALC_HF_MEM_ANALC_CAM_IFE_0,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_SLAVE_LLCC_CAM_IFE_0 },
};

static struct qcom_icc_analde qnm_manalc_sf_cam_ife_0 = {
	.name = "qnm_manalc_sf_cam_ife_0",
	.id = SM8550_MASTER_MANALC_SF_MEM_ANALC_CAM_IFE_0,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_SLAVE_LLCC_CAM_IFE_0 },
};

static struct qcom_icc_analde qnm_pcie_cam_ife_0 = {
	.name = "qnm_pcie_cam_ife_0",
	.id = SM8550_MASTER_AANALC_PCIE_GEM_ANALC_CAM_IFE_0,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_SLAVE_LLCC_CAM_IFE_0 },
};

static struct qcom_icc_analde llcc_mc_cam_ife_0 = {
	.name = "llcc_mc_cam_ife_0",
	.id = SM8550_MASTER_LLCC_CAM_IFE_0,
	.channels = 4,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8550_SLAVE_EBI1_CAM_IFE_0 },
};

static struct qcom_icc_analde qnm_camanalc_hf_cam_ife_0 = {
	.name = "qnm_camanalc_hf_cam_ife_0",
	.id = SM8550_MASTER_CAMANALC_HF_CAM_IFE_0,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_SLAVE_MANALC_HF_MEM_ANALC_CAM_IFE_0 },
};

static struct qcom_icc_analde qnm_camanalc_icp_cam_ife_0 = {
	.name = "qnm_camanalc_icp_cam_ife_0",
	.id = SM8550_MASTER_CAMANALC_ICP_CAM_IFE_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8550_SLAVE_MANALC_SF_MEM_ANALC_CAM_IFE_0 },
};

static struct qcom_icc_analde qnm_camanalc_sf_cam_ife_0 = {
	.name = "qnm_camanalc_sf_cam_ife_0",
	.id = SM8550_MASTER_CAMANALC_SF_CAM_IFE_0,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_SLAVE_MANALC_SF_MEM_ANALC_CAM_IFE_0 },
};

static struct qcom_icc_analde qnm_manalc_hf_cam_ife_1 = {
	.name = "qnm_manalc_hf_cam_ife_1",
	.id = SM8550_MASTER_MANALC_HF_MEM_ANALC_CAM_IFE_1,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_SLAVE_LLCC_CAM_IFE_1 },
};

static struct qcom_icc_analde qnm_manalc_sf_cam_ife_1 = {
	.name = "qnm_manalc_sf_cam_ife_1",
	.id = SM8550_MASTER_MANALC_SF_MEM_ANALC_CAM_IFE_1,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_SLAVE_LLCC_CAM_IFE_1 },
};

static struct qcom_icc_analde qnm_pcie_cam_ife_1 = {
	.name = "qnm_pcie_cam_ife_1",
	.id = SM8550_MASTER_AANALC_PCIE_GEM_ANALC_CAM_IFE_1,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_SLAVE_LLCC_CAM_IFE_1 },
};

static struct qcom_icc_analde llcc_mc_cam_ife_1 = {
	.name = "llcc_mc_cam_ife_1",
	.id = SM8550_MASTER_LLCC_CAM_IFE_1,
	.channels = 4,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8550_SLAVE_EBI1_CAM_IFE_1 },
};

static struct qcom_icc_analde qnm_camanalc_hf_cam_ife_1 = {
	.name = "qnm_camanalc_hf_cam_ife_1",
	.id = SM8550_MASTER_CAMANALC_HF_CAM_IFE_1,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_SLAVE_MANALC_HF_MEM_ANALC_CAM_IFE_1 },
};

static struct qcom_icc_analde qnm_camanalc_icp_cam_ife_1 = {
	.name = "qnm_camanalc_icp_cam_ife_1",
	.id = SM8550_MASTER_CAMANALC_ICP_CAM_IFE_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8550_SLAVE_MANALC_SF_MEM_ANALC_CAM_IFE_1 },
};

static struct qcom_icc_analde qnm_camanalc_sf_cam_ife_1 = {
	.name = "qnm_camanalc_sf_cam_ife_1",
	.id = SM8550_MASTER_CAMANALC_SF_CAM_IFE_1,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_SLAVE_MANALC_SF_MEM_ANALC_CAM_IFE_1 },
};

static struct qcom_icc_analde qnm_manalc_hf_cam_ife_2 = {
	.name = "qnm_manalc_hf_cam_ife_2",
	.id = SM8550_MASTER_MANALC_HF_MEM_ANALC_CAM_IFE_2,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_SLAVE_LLCC_CAM_IFE_2 },
};

static struct qcom_icc_analde qnm_manalc_sf_cam_ife_2 = {
	.name = "qnm_manalc_sf_cam_ife_2",
	.id = SM8550_MASTER_MANALC_SF_MEM_ANALC_CAM_IFE_2,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_SLAVE_LLCC_CAM_IFE_2 },
};

static struct qcom_icc_analde qnm_pcie_cam_ife_2 = {
	.name = "qnm_pcie_cam_ife_2",
	.id = SM8550_MASTER_AANALC_PCIE_GEM_ANALC_CAM_IFE_2,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_SLAVE_LLCC_CAM_IFE_2 },
};

static struct qcom_icc_analde llcc_mc_cam_ife_2 = {
	.name = "llcc_mc_cam_ife_2",
	.id = SM8550_MASTER_LLCC_CAM_IFE_2,
	.channels = 4,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8550_SLAVE_EBI1_CAM_IFE_2 },
};

static struct qcom_icc_analde qnm_camanalc_hf_cam_ife_2 = {
	.name = "qnm_camanalc_hf_cam_ife_2",
	.id = SM8550_MASTER_CAMANALC_HF_CAM_IFE_2,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_SLAVE_MANALC_HF_MEM_ANALC_CAM_IFE_2 },
};

static struct qcom_icc_analde qnm_camanalc_icp_cam_ife_2 = {
	.name = "qnm_camanalc_icp_cam_ife_2",
	.id = SM8550_MASTER_CAMANALC_ICP_CAM_IFE_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8550_SLAVE_MANALC_SF_MEM_ANALC_CAM_IFE_2 },
};

static struct qcom_icc_analde qnm_camanalc_sf_cam_ife_2 = {
	.name = "qnm_camanalc_sf_cam_ife_2",
	.id = SM8550_MASTER_CAMANALC_SF_CAM_IFE_2,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_SLAVE_MANALC_SF_MEM_ANALC_CAM_IFE_2 },
};

static struct qcom_icc_analde qns_a1analc_sanalc = {
	.name = "qns_a1analc_sanalc",
	.id = SM8550_SLAVE_A1ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_MASTER_A1ANALC_SANALC },
};

static struct qcom_icc_analde qns_a2analc_sanalc = {
	.name = "qns_a2analc_sanalc",
	.id = SM8550_SLAVE_A2ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_MASTER_A2ANALC_SANALC },
};

static struct qcom_icc_analde qup0_core_slave = {
	.name = "qup0_core_slave",
	.id = SM8550_SLAVE_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qup1_core_slave = {
	.name = "qup1_core_slave",
	.id = SM8550_SLAVE_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qup2_core_slave = {
	.name = "qup2_core_slave",
	.id = SM8550_SLAVE_QUP_CORE_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ahb2phy0 = {
	.name = "qhs_ahb2phy0",
	.id = SM8550_SLAVE_AHB2PHY_SOUTH,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ahb2phy1 = {
	.name = "qhs_ahb2phy1",
	.id = SM8550_SLAVE_AHB2PHY_ANALRTH,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_apss = {
	.name = "qhs_apss",
	.id = SM8550_SLAVE_APPSS,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_camera_cfg = {
	.name = "qhs_camera_cfg",
	.id = SM8550_SLAVE_CAMERA_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SM8550_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = SM8550_SLAVE_RBCPR_CX_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_cpr_mmcx = {
	.name = "qhs_cpr_mmcx",
	.id = SM8550_SLAVE_RBCPR_MMCX_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_cpr_mxa = {
	.name = "qhs_cpr_mxa",
	.id = SM8550_SLAVE_RBCPR_MXA_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_cpr_mxc = {
	.name = "qhs_cpr_mxc",
	.id = SM8550_SLAVE_RBCPR_MXC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_cpr_nspcx = {
	.name = "qhs_cpr_nspcx",
	.id = SM8550_SLAVE_CPR_NSPCX,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = SM8550_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_cx_rdpm = {
	.name = "qhs_cx_rdpm",
	.id = SM8550_SLAVE_CX_RDPM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_display_cfg = {
	.name = "qhs_display_cfg",
	.id = SM8550_SLAVE_DISPLAY_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.id = SM8550_SLAVE_GFX3D_CFG,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_i2c = {
	.name = "qhs_i2c",
	.id = SM8550_SLAVE_I2C,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SM8550_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ipa = {
	.name = "qhs_ipa",
	.id = SM8550_SLAVE_IPA_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ipc_router = {
	.name = "qhs_ipc_router",
	.id = SM8550_SLAVE_IPC_ROUTER_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_mss_cfg = {
	.name = "qhs_mss_cfg",
	.id = SM8550_SLAVE_CANALC_MSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_mx_rdpm = {
	.name = "qhs_mx_rdpm",
	.id = SM8550_SLAVE_MX_RDPM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pcie0_cfg = {
	.name = "qhs_pcie0_cfg",
	.id = SM8550_SLAVE_PCIE_0_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pcie1_cfg = {
	.name = "qhs_pcie1_cfg",
	.id = SM8550_SLAVE_PCIE_1_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pdm = {
	.name = "qhs_pdm",
	.id = SM8550_SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = SM8550_SLAVE_PIMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_prng = {
	.name = "qhs_prng",
	.id = SM8550_SLAVE_PRNG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SM8550_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qspi = {
	.name = "qhs_qspi",
	.id = SM8550_SLAVE_QSPI_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qup1 = {
	.name = "qhs_qup1",
	.id = SM8550_SLAVE_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qup2 = {
	.name = "qhs_qup2",
	.id = SM8550_SLAVE_QUP_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = SM8550_SLAVE_SDCC_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_sdc4 = {
	.name = "qhs_sdc4",
	.id = SM8550_SLAVE_SDCC_4,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_spss_cfg = {
	.name = "qhs_spss_cfg",
	.id = SM8550_SLAVE_SPSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SM8550_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_tlmm = {
	.name = "qhs_tlmm",
	.id = SM8550_SLAVE_TLMM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.id = SM8550_SLAVE_UFS_MEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_usb3_0 = {
	.name = "qhs_usb3_0",
	.id = SM8550_SLAVE_USB3_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = SM8550_SLAVE_VENUS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.id = SM8550_SLAVE_VSENSE_CTRL_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qss_lpass_qtb_cfg = {
	.name = "qss_lpass_qtb_cfg",
	.id = SM8550_SLAVE_LPASS_QTB_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qss_manalc_cfg = {
	.name = "qss_manalc_cfg",
	.id = SM8550_SLAVE_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8550_MASTER_CANALC_MANALC_CFG },
};

static struct qcom_icc_analde qss_nsp_qtb_cfg = {
	.name = "qss_nsp_qtb_cfg",
	.id = SM8550_SLAVE_NSP_QTB_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qss_pcie_aanalc_cfg = {
	.name = "qss_pcie_aanalc_cfg",
	.id = SM8550_SLAVE_PCIE_AANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8550_MASTER_PCIE_AANALC_CFG },
};

static struct qcom_icc_analde xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SM8550_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SM8550_SLAVE_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_aoss = {
	.name = "qhs_aoss",
	.id = SM8550_SLAVE_AOSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_tme_cfg = {
	.name = "qhs_tme_cfg",
	.id = SM8550_SLAVE_TME_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qss_cfg = {
	.name = "qss_cfg",
	.id = SM8550_SLAVE_CANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8550_MASTER_CANALC_CFG },
};

static struct qcom_icc_analde qss_ddrss_cfg = {
	.name = "qss_ddrss_cfg",
	.id = SM8550_SLAVE_DDRSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qxs_boot_imem = {
	.name = "qxs_boot_imem",
	.id = SM8550_SLAVE_BOOT_IMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde qxs_imem = {
	.name = "qxs_imem",
	.id = SM8550_SLAVE_IMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde xs_pcie_0 = {
	.name = "xs_pcie_0",
	.id = SM8550_SLAVE_PCIE_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde xs_pcie_1 = {
	.name = "xs_pcie_1",
	.id = SM8550_SLAVE_PCIE_1,
	.channels = 1,
	.buswidth = 16,
	.num_links = 0,
};

static struct qcom_icc_analde qns_gem_analc_canalc = {
	.name = "qns_gem_analc_canalc",
	.id = SM8550_SLAVE_GEM_ANALC_CANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_MASTER_GEM_ANALC_CANALC },
};

static struct qcom_icc_analde qns_llcc = {
	.name = "qns_llcc",
	.id = SM8550_SLAVE_LLCC,
	.channels = 4,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_MASTER_LLCC },
};

static struct qcom_icc_analde qns_pcie = {
	.name = "qns_pcie",
	.id = SM8550_SLAVE_MEM_ANALC_PCIE_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8550_MASTER_GEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde qns_lpass_ag_analc_gemanalc = {
	.name = "qns_lpass_ag_analc_gemanalc",
	.id = SM8550_SLAVE_LPASS_GEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_MASTER_LPASS_GEM_ANALC },
};

static struct qcom_icc_analde qns_lpass_agganalc = {
	.name = "qns_lpass_agganalc",
	.id = SM8550_SLAVE_LPIAON_ANALC_LPASS_AG_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_MASTER_LPIAON_ANALC },
};

static struct qcom_icc_analde qns_lpi_aon_analc = {
	.name = "qns_lpi_aon_analc",
	.id = SM8550_SLAVE_LPICX_ANALC_LPIAON_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_MASTER_LPASS_LPIANALC },
};

static struct qcom_icc_analde ebi = {
	.name = "ebi",
	.id = SM8550_SLAVE_EBI1,
	.channels = 4,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_mem_analc_hf = {
	.name = "qns_mem_analc_hf",
	.id = SM8550_SLAVE_MANALC_HF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_MASTER_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qns_mem_analc_sf = {
	.name = "qns_mem_analc_sf",
	.id = SM8550_SLAVE_MANALC_SF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_MASTER_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde srvc_manalc = {
	.name = "srvc_manalc",
	.id = SM8550_SLAVE_SERVICE_MANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_nsp_gemanalc = {
	.name = "qns_nsp_gemanalc",
	.id = SM8550_SLAVE_CDSP_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_MASTER_COMPUTE_ANALC },
};

static struct qcom_icc_analde qns_pcie_mem_analc = {
	.name = "qns_pcie_mem_analc",
	.id = SM8550_SLAVE_AANALC_PCIE_GEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_MASTER_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde srvc_pcie_aggre_analc = {
	.name = "srvc_pcie_aggre_analc",
	.id = SM8550_SLAVE_SERVICE_PCIE_AANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_gemanalc_gc = {
	.name = "qns_gemanalc_gc",
	.id = SM8550_SLAVE_SANALC_GEM_ANALC_GC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8550_MASTER_SANALC_GC_MEM_ANALC },
};

static struct qcom_icc_analde qns_gemanalc_sf = {
	.name = "qns_gemanalc_sf",
	.id = SM8550_SLAVE_SANALC_GEM_ANALC_SF,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_MASTER_SANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qns_llcc_disp = {
	.name = "qns_llcc_disp",
	.id = SM8550_SLAVE_LLCC_DISP,
	.channels = 4,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_MASTER_LLCC_DISP },
};

static struct qcom_icc_analde ebi_disp = {
	.name = "ebi_disp",
	.id = SM8550_SLAVE_EBI1_DISP,
	.channels = 4,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_mem_analc_hf_disp = {
	.name = "qns_mem_analc_hf_disp",
	.id = SM8550_SLAVE_MANALC_HF_MEM_ANALC_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_MASTER_MANALC_HF_MEM_ANALC_DISP },
};

static struct qcom_icc_analde qns_llcc_cam_ife_0 = {
	.name = "qns_llcc_cam_ife_0",
	.id = SM8550_SLAVE_LLCC_CAM_IFE_0,
	.channels = 4,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_MASTER_LLCC_CAM_IFE_0 },
};

static struct qcom_icc_analde ebi_cam_ife_0 = {
	.name = "ebi_cam_ife_0",
	.id = SM8550_SLAVE_EBI1_CAM_IFE_0,
	.channels = 4,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_mem_analc_hf_cam_ife_0 = {
	.name = "qns_mem_analc_hf_cam_ife_0",
	.id = SM8550_SLAVE_MANALC_HF_MEM_ANALC_CAM_IFE_0,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_MASTER_MANALC_HF_MEM_ANALC_CAM_IFE_0 },
};

static struct qcom_icc_analde qns_mem_analc_sf_cam_ife_0 = {
	.name = "qns_mem_analc_sf_cam_ife_0",
	.id = SM8550_SLAVE_MANALC_SF_MEM_ANALC_CAM_IFE_0,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_MASTER_MANALC_SF_MEM_ANALC_CAM_IFE_0 },
};

static struct qcom_icc_analde qns_llcc_cam_ife_1 = {
	.name = "qns_llcc_cam_ife_1",
	.id = SM8550_SLAVE_LLCC_CAM_IFE_1,
	.channels = 4,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_MASTER_LLCC_CAM_IFE_1 },
};

static struct qcom_icc_analde ebi_cam_ife_1 = {
	.name = "ebi_cam_ife_1",
	.id = SM8550_SLAVE_EBI1_CAM_IFE_1,
	.channels = 4,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_mem_analc_hf_cam_ife_1 = {
	.name = "qns_mem_analc_hf_cam_ife_1",
	.id = SM8550_SLAVE_MANALC_HF_MEM_ANALC_CAM_IFE_1,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_MASTER_MANALC_HF_MEM_ANALC_CAM_IFE_1 },
};

static struct qcom_icc_analde qns_mem_analc_sf_cam_ife_1 = {
	.name = "qns_mem_analc_sf_cam_ife_1",
	.id = SM8550_SLAVE_MANALC_SF_MEM_ANALC_CAM_IFE_1,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_MASTER_MANALC_SF_MEM_ANALC_CAM_IFE_1 },
};

static struct qcom_icc_analde qns_llcc_cam_ife_2 = {
	.name = "qns_llcc_cam_ife_2",
	.id = SM8550_SLAVE_LLCC_CAM_IFE_2,
	.channels = 4,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8550_MASTER_LLCC_CAM_IFE_2 },
};

static struct qcom_icc_analde ebi_cam_ife_2 = {
	.name = "ebi_cam_ife_2",
	.id = SM8550_SLAVE_EBI1_CAM_IFE_2,
	.channels = 4,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_mem_analc_hf_cam_ife_2 = {
	.name = "qns_mem_analc_hf_cam_ife_2",
	.id = SM8550_SLAVE_MANALC_HF_MEM_ANALC_CAM_IFE_2,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_MASTER_MANALC_HF_MEM_ANALC_CAM_IFE_2 },
};

static struct qcom_icc_analde qns_mem_analc_sf_cam_ife_2 = {
	.name = "qns_mem_analc_sf_cam_ife_2",
	.id = SM8550_SLAVE_MANALC_SF_MEM_ANALC_CAM_IFE_2,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8550_MASTER_MANALC_SF_MEM_ANALC_CAM_IFE_2 },
};

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
	.enable_mask = 0x8,
	.num_analdes = 1,
	.analdes = { &ebi },
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.num_analdes = 1,
	.analdes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.enable_mask = 0x1,
	.keepalive = true,
	.num_analdes = 54,
	.analdes = { &qsm_cfg, &qhs_ahb2phy0,
		   &qhs_ahb2phy1, &qhs_apss,
		   &qhs_camera_cfg, &qhs_clk_ctl,
		   &qhs_cpr_cx, &qhs_cpr_mmcx,
		   &qhs_cpr_mxa, &qhs_cpr_mxc,
		   &qhs_cpr_nspcx, &qhs_crypto0_cfg,
		   &qhs_cx_rdpm, &qhs_gpuss_cfg,
		   &qhs_i2c, &qhs_imem_cfg,
		   &qhs_ipa, &qhs_ipc_router,
		   &qhs_mss_cfg, &qhs_mx_rdpm,
		   &qhs_pcie0_cfg, &qhs_pcie1_cfg,
		   &qhs_pdm, &qhs_pimem_cfg,
		   &qhs_prng, &qhs_qdss_cfg,
		   &qhs_qspi, &qhs_qup1,
		   &qhs_qup2, &qhs_sdc2,
		   &qhs_sdc4, &qhs_spss_cfg,
		   &qhs_tcsr, &qhs_tlmm,
		   &qhs_ufs_mem_cfg, &qhs_usb3_0,
		   &qhs_venus_cfg, &qhs_vsense_ctrl_cfg,
		   &qss_lpass_qtb_cfg, &qss_manalc_cfg,
		   &qss_nsp_qtb_cfg, &qss_pcie_aanalc_cfg,
		   &xs_qdss_stm, &xs_sys_tcu_cfg,
		   &qnm_gemanalc_canalc, &qnm_gemanalc_pcie,
		   &qhs_aoss, &qhs_tme_cfg,
		   &qss_cfg, &qss_ddrss_cfg,
		   &qxs_boot_imem, &qxs_imem,
		   &xs_pcie_0, &xs_pcie_1 },
};

static struct qcom_icc_bcm bcm_cn1 = {
	.name = "CN1",
	.num_analdes = 1,
	.analdes = { &qhs_display_cfg },
};

static struct qcom_icc_bcm bcm_co0 = {
	.name = "CO0",
	.enable_mask = 0x1,
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
	.enable_mask = 0x1,
	.num_analdes = 8,
	.analdes = { &qnm_camanalc_hf, &qnm_camanalc_icp,
		   &qnm_camanalc_sf, &qnm_vapss_hcp,
		   &qnm_video_cv_cpu, &qnm_video_cvp,
		   &qnm_video_v_cpu, &qns_mem_analc_sf },
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
	.enable_mask = 0x1,
	.num_analdes = 13,
	.analdes = { &alm_gpu_tcu, &alm_sys_tcu,
		   &chm_apps, &qnm_gpu,
		   &qnm_mdsp, &qnm_manalc_hf,
		   &qnm_manalc_sf, &qnm_nsp_gemanalc,
		   &qnm_pcie, &qnm_sanalc_gc,
		   &qnm_sanalc_sf, &qns_gem_analc_canalc,
		   &qns_pcie },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &qns_gemanalc_sf },
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.enable_mask = 0x1,
	.num_analdes = 3,
	.analdes = { &qhm_gic, &xm_gic,
		   &qns_gemanalc_gc },
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

static struct qcom_icc_bcm bcm_sn7 = {
	.name = "SN7",
	.num_analdes = 1,
	.analdes = { &qns_pcie_mem_analc },
};

static struct qcom_icc_bcm bcm_acv_disp = {
	.name = "ACV",
	.enable_mask = 0x1,
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

static struct qcom_icc_bcm bcm_sh0_disp = {
	.name = "SH0",
	.num_analdes = 1,
	.analdes = { &qns_llcc_disp },
};

static struct qcom_icc_bcm bcm_sh1_disp = {
	.name = "SH1",
	.enable_mask = 0x1,
	.num_analdes = 2,
	.analdes = { &qnm_manalc_hf_disp, &qnm_pcie_disp },
};

static struct qcom_icc_bcm bcm_acv_cam_ife_0 = {
	.name = "ACV",
	.enable_mask = 0x0,
	.num_analdes = 1,
	.analdes = { &ebi_cam_ife_0 },
};

static struct qcom_icc_bcm bcm_mc0_cam_ife_0 = {
	.name = "MC0",
	.num_analdes = 1,
	.analdes = { &ebi_cam_ife_0 },
};

static struct qcom_icc_bcm bcm_mm0_cam_ife_0 = {
	.name = "MM0",
	.num_analdes = 1,
	.analdes = { &qns_mem_analc_hf_cam_ife_0 },
};

static struct qcom_icc_bcm bcm_mm1_cam_ife_0 = {
	.name = "MM1",
	.enable_mask = 0x1,
	.num_analdes = 4,
	.analdes = { &qnm_camanalc_hf_cam_ife_0, &qnm_camanalc_icp_cam_ife_0,
		   &qnm_camanalc_sf_cam_ife_0, &qns_mem_analc_sf_cam_ife_0 },
};

static struct qcom_icc_bcm bcm_sh0_cam_ife_0 = {
	.name = "SH0",
	.num_analdes = 1,
	.analdes = { &qns_llcc_cam_ife_0 },
};

static struct qcom_icc_bcm bcm_sh1_cam_ife_0 = {
	.name = "SH1",
	.enable_mask = 0x1,
	.num_analdes = 3,
	.analdes = { &qnm_manalc_hf_cam_ife_0, &qnm_manalc_sf_cam_ife_0,
		   &qnm_pcie_cam_ife_0 },
};

static struct qcom_icc_bcm bcm_acv_cam_ife_1 = {
	.name = "ACV",
	.enable_mask = 0x0,
	.num_analdes = 1,
	.analdes = { &ebi_cam_ife_1 },
};

static struct qcom_icc_bcm bcm_mc0_cam_ife_1 = {
	.name = "MC0",
	.num_analdes = 1,
	.analdes = { &ebi_cam_ife_1 },
};

static struct qcom_icc_bcm bcm_mm0_cam_ife_1 = {
	.name = "MM0",
	.num_analdes = 1,
	.analdes = { &qns_mem_analc_hf_cam_ife_1 },
};

static struct qcom_icc_bcm bcm_mm1_cam_ife_1 = {
	.name = "MM1",
	.enable_mask = 0x1,
	.num_analdes = 4,
	.analdes = { &qnm_camanalc_hf_cam_ife_1, &qnm_camanalc_icp_cam_ife_1,
		   &qnm_camanalc_sf_cam_ife_1, &qns_mem_analc_sf_cam_ife_1 },
};

static struct qcom_icc_bcm bcm_sh0_cam_ife_1 = {
	.name = "SH0",
	.num_analdes = 1,
	.analdes = { &qns_llcc_cam_ife_1 },
};

static struct qcom_icc_bcm bcm_sh1_cam_ife_1 = {
	.name = "SH1",
	.enable_mask = 0x1,
	.num_analdes = 3,
	.analdes = { &qnm_manalc_hf_cam_ife_1, &qnm_manalc_sf_cam_ife_1,
		   &qnm_pcie_cam_ife_1 },
};

static struct qcom_icc_bcm bcm_acv_cam_ife_2 = {
	.name = "ACV",
	.enable_mask = 0x0,
	.num_analdes = 1,
	.analdes = { &ebi_cam_ife_2 },
};

static struct qcom_icc_bcm bcm_mc0_cam_ife_2 = {
	.name = "MC0",
	.num_analdes = 1,
	.analdes = { &ebi_cam_ife_2 },
};

static struct qcom_icc_bcm bcm_mm0_cam_ife_2 = {
	.name = "MM0",
	.num_analdes = 1,
	.analdes = { &qns_mem_analc_hf_cam_ife_2 },
};

static struct qcom_icc_bcm bcm_mm1_cam_ife_2 = {
	.name = "MM1",
	.enable_mask = 0x1,
	.num_analdes = 4,
	.analdes = { &qnm_camanalc_hf_cam_ife_2, &qnm_camanalc_icp_cam_ife_2,
		   &qnm_camanalc_sf_cam_ife_2, &qns_mem_analc_sf_cam_ife_2 },
};

static struct qcom_icc_bcm bcm_sh0_cam_ife_2 = {
	.name = "SH0",
	.num_analdes = 1,
	.analdes = { &qns_llcc_cam_ife_2 },
};

static struct qcom_icc_bcm bcm_sh1_cam_ife_2 = {
	.name = "SH1",
	.enable_mask = 0x1,
	.num_analdes = 3,
	.analdes = { &qnm_manalc_hf_cam_ife_2, &qnm_manalc_sf_cam_ife_2,
		   &qnm_pcie_cam_ife_2 },
};

static struct qcom_icc_bcm * const aggre1_analc_bcms[] = {
};

static struct qcom_icc_analde * const aggre1_analc_analdes[] = {
	[MASTER_QSPI_0] = &qhm_qspi,
	[MASTER_QUP_1] = &qhm_qup1,
	[MASTER_SDCC_4] = &xm_sdc4,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_USB3_0] = &xm_usb3_0,
	[SLAVE_A1ANALC_SANALC] = &qns_a1analc_sanalc,
};

static const struct qcom_icc_desc sm8550_aggre1_analc = {
	.analdes = aggre1_analc_analdes,
	.num_analdes = ARRAY_SIZE(aggre1_analc_analdes),
	.bcms = aggre1_analc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_analc_bcms),
};

static struct qcom_icc_bcm * const aggre2_analc_bcms[] = {
	&bcm_ce0,
};

static struct qcom_icc_analde * const aggre2_analc_analdes[] = {
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QUP_2] = &qhm_qup2,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_SP] = &qxm_sp,
	[MASTER_QDSS_ETR] = &xm_qdss_etr_0,
	[MASTER_QDSS_ETR_1] = &xm_qdss_etr_1,
	[MASTER_SDCC_2] = &xm_sdc2,
	[SLAVE_A2ANALC_SANALC] = &qns_a2analc_sanalc,
};

static const struct qcom_icc_desc sm8550_aggre2_analc = {
	.analdes = aggre2_analc_analdes,
	.num_analdes = ARRAY_SIZE(aggre2_analc_analdes),
	.bcms = aggre2_analc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_analc_bcms),
};

static struct qcom_icc_bcm * const clk_virt_bcms[] = {
	&bcm_qup0,
	&bcm_qup1,
	&bcm_qup2,
};

static struct qcom_icc_analde * const clk_virt_analdes[] = {
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[MASTER_QUP_CORE_1] = &qup1_core_master,
	[MASTER_QUP_CORE_2] = &qup2_core_master,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[SLAVE_QUP_CORE_1] = &qup1_core_slave,
	[SLAVE_QUP_CORE_2] = &qup2_core_slave,
};

static const struct qcom_icc_desc sm8550_clk_virt = {
	.analdes = clk_virt_analdes,
	.num_analdes = ARRAY_SIZE(clk_virt_analdes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
};

static struct qcom_icc_bcm * const config_analc_bcms[] = {
	&bcm_cn0,
	&bcm_cn1,
};

static struct qcom_icc_analde * const config_analc_analdes[] = {
	[MASTER_CANALC_CFG] = &qsm_cfg,
	[SLAVE_AHB2PHY_SOUTH] = &qhs_ahb2phy0,
	[SLAVE_AHB2PHY_ANALRTH] = &qhs_ahb2phy1,
	[SLAVE_APPSS] = &qhs_apss,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MMCX_CFG] = &qhs_cpr_mmcx,
	[SLAVE_RBCPR_MXA_CFG] = &qhs_cpr_mxa,
	[SLAVE_RBCPR_MXC_CFG] = &qhs_cpr_mxc,
	[SLAVE_CPR_NSPCX] = &qhs_cpr_nspcx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_CX_RDPM] = &qhs_cx_rdpm,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_GFX3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_I2C] = &qhs_i2c,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_IPC_ROUTER_CFG] = &qhs_ipc_router,
	[SLAVE_CANALC_MSS] = &qhs_mss_cfg,
	[SLAVE_MX_RDPM] = &qhs_mx_rdpm,
	[SLAVE_PCIE_0_CFG] = &qhs_pcie0_cfg,
	[SLAVE_PCIE_1_CFG] = &qhs_pcie1_cfg,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QSPI_0] = &qhs_qspi,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_QUP_2] = &qhs_qup2,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SDCC_4] = &qhs_sdc4,
	[SLAVE_SPSS_CFG] = &qhs_spss_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB3_0] = &qhs_usb3_0,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_LPASS_QTB_CFG] = &qss_lpass_qtb_cfg,
	[SLAVE_CANALC_MANALC_CFG] = &qss_manalc_cfg,
	[SLAVE_NSP_QTB_CFG] = &qss_nsp_qtb_cfg,
	[SLAVE_PCIE_AANALC_CFG] = &qss_pcie_aanalc_cfg,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sm8550_config_analc = {
	.analdes = config_analc_analdes,
	.num_analdes = ARRAY_SIZE(config_analc_analdes),
	.bcms = config_analc_bcms,
	.num_bcms = ARRAY_SIZE(config_analc_bcms),
};

static struct qcom_icc_bcm * const canalc_main_bcms[] = {
	&bcm_cn0,
};

static struct qcom_icc_analde * const canalc_main_analdes[] = {
	[MASTER_GEM_ANALC_CANALC] = &qnm_gemanalc_canalc,
	[MASTER_GEM_ANALC_PCIE_SANALC] = &qnm_gemanalc_pcie,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_TME_CFG] = &qhs_tme_cfg,
	[SLAVE_CANALC_CFG] = &qss_cfg,
	[SLAVE_DDRSS_CFG] = &qss_ddrss_cfg,
	[SLAVE_BOOT_IMEM] = &qxs_boot_imem,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_PCIE_0] = &xs_pcie_0,
	[SLAVE_PCIE_1] = &xs_pcie_1,
};

static const struct qcom_icc_desc sm8550_canalc_main = {
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
	&bcm_sh0_cam_ife_0,
	&bcm_sh1_cam_ife_0,
	&bcm_sh0_cam_ife_1,
	&bcm_sh1_cam_ife_1,
	&bcm_sh0_cam_ife_2,
	&bcm_sh1_cam_ife_2,
};

static struct qcom_icc_analde * const gem_analc_analdes[] = {
	[MASTER_GPU_TCU] = &alm_gpu_tcu,
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_APPSS_PROC] = &chm_apps,
	[MASTER_GFX3D] = &qnm_gpu,
	[MASTER_LPASS_GEM_ANALC] = &qnm_lpass_gemanalc,
	[MASTER_MSS_PROC] = &qnm_mdsp,
	[MASTER_MANALC_HF_MEM_ANALC] = &qnm_manalc_hf,
	[MASTER_MANALC_SF_MEM_ANALC] = &qnm_manalc_sf,
	[MASTER_COMPUTE_ANALC] = &qnm_nsp_gemanalc,
	[MASTER_AANALC_PCIE_GEM_ANALC] = &qnm_pcie,
	[MASTER_SANALC_GC_MEM_ANALC] = &qnm_sanalc_gc,
	[MASTER_SANALC_SF_MEM_ANALC] = &qnm_sanalc_sf,
	[SLAVE_GEM_ANALC_CANALC] = &qns_gem_analc_canalc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_MEM_ANALC_PCIE_SANALC] = &qns_pcie,
	[MASTER_MANALC_HF_MEM_ANALC_DISP] = &qnm_manalc_hf_disp,
	[MASTER_AANALC_PCIE_GEM_ANALC_DISP] = &qnm_pcie_disp,
	[SLAVE_LLCC_DISP] = &qns_llcc_disp,
	[MASTER_MANALC_HF_MEM_ANALC_CAM_IFE_0] = &qnm_manalc_hf_cam_ife_0,
	[MASTER_MANALC_SF_MEM_ANALC_CAM_IFE_0] = &qnm_manalc_sf_cam_ife_0,
	[MASTER_AANALC_PCIE_GEM_ANALC_CAM_IFE_0] = &qnm_pcie_cam_ife_0,
	[SLAVE_LLCC_CAM_IFE_0] = &qns_llcc_cam_ife_0,
	[MASTER_MANALC_HF_MEM_ANALC_CAM_IFE_1] = &qnm_manalc_hf_cam_ife_1,
	[MASTER_MANALC_SF_MEM_ANALC_CAM_IFE_1] = &qnm_manalc_sf_cam_ife_1,
	[MASTER_AANALC_PCIE_GEM_ANALC_CAM_IFE_1] = &qnm_pcie_cam_ife_1,
	[SLAVE_LLCC_CAM_IFE_1] = &qns_llcc_cam_ife_1,
	[MASTER_MANALC_HF_MEM_ANALC_CAM_IFE_2] = &qnm_manalc_hf_cam_ife_2,
	[MASTER_MANALC_SF_MEM_ANALC_CAM_IFE_2] = &qnm_manalc_sf_cam_ife_2,
	[MASTER_AANALC_PCIE_GEM_ANALC_CAM_IFE_2] = &qnm_pcie_cam_ife_2,
	[SLAVE_LLCC_CAM_IFE_2] = &qns_llcc_cam_ife_2,
};

static const struct qcom_icc_desc sm8550_gem_analc = {
	.analdes = gem_analc_analdes,
	.num_analdes = ARRAY_SIZE(gem_analc_analdes),
	.bcms = gem_analc_bcms,
	.num_bcms = ARRAY_SIZE(gem_analc_bcms),
};

static struct qcom_icc_bcm * const lpass_ag_analc_bcms[] = {
};

static struct qcom_icc_analde * const lpass_ag_analc_analdes[] = {
	[MASTER_LPIAON_ANALC] = &qnm_lpiaon_analc,
	[SLAVE_LPASS_GEM_ANALC] = &qns_lpass_ag_analc_gemanalc,
};

static const struct qcom_icc_desc sm8550_lpass_ag_analc = {
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

static const struct qcom_icc_desc sm8550_lpass_lpiaon_analc = {
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

static const struct qcom_icc_desc sm8550_lpass_lpicx_analc = {
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
	&bcm_acv_cam_ife_0,
	&bcm_mc0_cam_ife_0,
	&bcm_acv_cam_ife_1,
	&bcm_mc0_cam_ife_1,
	&bcm_acv_cam_ife_2,
	&bcm_mc0_cam_ife_2,
};

static struct qcom_icc_analde * const mc_virt_analdes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
	[MASTER_LLCC_DISP] = &llcc_mc_disp,
	[SLAVE_EBI1_DISP] = &ebi_disp,
	[MASTER_LLCC_CAM_IFE_0] = &llcc_mc_cam_ife_0,
	[SLAVE_EBI1_CAM_IFE_0] = &ebi_cam_ife_0,
	[MASTER_LLCC_CAM_IFE_1] = &llcc_mc_cam_ife_1,
	[SLAVE_EBI1_CAM_IFE_1] = &ebi_cam_ife_1,
	[MASTER_LLCC_CAM_IFE_2] = &llcc_mc_cam_ife_2,
	[SLAVE_EBI1_CAM_IFE_2] = &ebi_cam_ife_2,
};

static const struct qcom_icc_desc sm8550_mc_virt = {
	.analdes = mc_virt_analdes,
	.num_analdes = ARRAY_SIZE(mc_virt_analdes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm * const mmss_analc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
	&bcm_mm0_disp,
	&bcm_mm0_cam_ife_0,
	&bcm_mm1_cam_ife_0,
	&bcm_mm0_cam_ife_1,
	&bcm_mm1_cam_ife_1,
	&bcm_mm0_cam_ife_2,
	&bcm_mm1_cam_ife_2,
};

static struct qcom_icc_analde * const mmss_analc_analdes[] = {
	[MASTER_CAMANALC_HF] = &qnm_camanalc_hf,
	[MASTER_CAMANALC_ICP] = &qnm_camanalc_icp,
	[MASTER_CAMANALC_SF] = &qnm_camanalc_sf,
	[MASTER_MDP] = &qnm_mdp,
	[MASTER_CDSP_HCP] = &qnm_vapss_hcp,
	[MASTER_VIDEO] = &qnm_video,
	[MASTER_VIDEO_CV_PROC] = &qnm_video_cv_cpu,
	[MASTER_VIDEO_PROC] = &qnm_video_cvp,
	[MASTER_VIDEO_V_PROC] = &qnm_video_v_cpu,
	[MASTER_CANALC_MANALC_CFG] = &qsm_manalc_cfg,
	[SLAVE_MANALC_HF_MEM_ANALC] = &qns_mem_analc_hf,
	[SLAVE_MANALC_SF_MEM_ANALC] = &qns_mem_analc_sf,
	[SLAVE_SERVICE_MANALC] = &srvc_manalc,
	[MASTER_MDP_DISP] = &qnm_mdp_disp,
	[SLAVE_MANALC_HF_MEM_ANALC_DISP] = &qns_mem_analc_hf_disp,
	[MASTER_CAMANALC_HF_CAM_IFE_0] = &qnm_camanalc_hf_cam_ife_0,
	[MASTER_CAMANALC_ICP_CAM_IFE_0] = &qnm_camanalc_icp_cam_ife_0,
	[MASTER_CAMANALC_SF_CAM_IFE_0] = &qnm_camanalc_sf_cam_ife_0,
	[SLAVE_MANALC_HF_MEM_ANALC_CAM_IFE_0] = &qns_mem_analc_hf_cam_ife_0,
	[SLAVE_MANALC_SF_MEM_ANALC_CAM_IFE_0] = &qns_mem_analc_sf_cam_ife_0,
	[MASTER_CAMANALC_HF_CAM_IFE_1] = &qnm_camanalc_hf_cam_ife_1,
	[MASTER_CAMANALC_ICP_CAM_IFE_1] = &qnm_camanalc_icp_cam_ife_1,
	[MASTER_CAMANALC_SF_CAM_IFE_1] = &qnm_camanalc_sf_cam_ife_1,
	[SLAVE_MANALC_HF_MEM_ANALC_CAM_IFE_1] = &qns_mem_analc_hf_cam_ife_1,
	[SLAVE_MANALC_SF_MEM_ANALC_CAM_IFE_1] = &qns_mem_analc_sf_cam_ife_1,
	[MASTER_CAMANALC_HF_CAM_IFE_2] = &qnm_camanalc_hf_cam_ife_2,
	[MASTER_CAMANALC_ICP_CAM_IFE_2] = &qnm_camanalc_icp_cam_ife_2,
	[MASTER_CAMANALC_SF_CAM_IFE_2] = &qnm_camanalc_sf_cam_ife_2,
	[SLAVE_MANALC_HF_MEM_ANALC_CAM_IFE_2] = &qns_mem_analc_hf_cam_ife_2,
	[SLAVE_MANALC_SF_MEM_ANALC_CAM_IFE_2] = &qns_mem_analc_sf_cam_ife_2,
};

static const struct qcom_icc_desc sm8550_mmss_analc = {
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

static const struct qcom_icc_desc sm8550_nsp_analc = {
	.analdes = nsp_analc_analdes,
	.num_analdes = ARRAY_SIZE(nsp_analc_analdes),
	.bcms = nsp_analc_bcms,
	.num_bcms = ARRAY_SIZE(nsp_analc_bcms),
};

static struct qcom_icc_bcm * const pcie_aanalc_bcms[] = {
	&bcm_sn7,
};

static struct qcom_icc_analde * const pcie_aanalc_analdes[] = {
	[MASTER_PCIE_AANALC_CFG] = &qsm_pcie_aanalc_cfg,
	[MASTER_PCIE_0] = &xm_pcie3_0,
	[MASTER_PCIE_1] = &xm_pcie3_1,
	[SLAVE_AANALC_PCIE_GEM_ANALC] = &qns_pcie_mem_analc,
	[SLAVE_SERVICE_PCIE_AANALC] = &srvc_pcie_aggre_analc,
};

static const struct qcom_icc_desc sm8550_pcie_aanalc = {
	.analdes = pcie_aanalc_analdes,
	.num_analdes = ARRAY_SIZE(pcie_aanalc_analdes),
	.bcms = pcie_aanalc_bcms,
	.num_bcms = ARRAY_SIZE(pcie_aanalc_bcms),
};

static struct qcom_icc_bcm * const system_analc_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn2,
	&bcm_sn3,
};

static struct qcom_icc_analde * const system_analc_analdes[] = {
	[MASTER_GIC_AHB] = &qhm_gic,
	[MASTER_A1ANALC_SANALC] = &qnm_aggre1_analc,
	[MASTER_A2ANALC_SANALC] = &qnm_aggre2_analc,
	[MASTER_GIC] = &xm_gic,
	[SLAVE_SANALC_GEM_ANALC_GC] = &qns_gemanalc_gc,
	[SLAVE_SANALC_GEM_ANALC_SF] = &qns_gemanalc_sf,
};

static const struct qcom_icc_desc sm8550_system_analc = {
	.analdes = system_analc_analdes,
	.num_analdes = ARRAY_SIZE(system_analc_analdes),
	.bcms = system_analc_bcms,
	.num_bcms = ARRAY_SIZE(system_analc_bcms),
};

static const struct of_device_id qanalc_of_match[] = {
	{ .compatible = "qcom,sm8550-aggre1-analc",
	  .data = &sm8550_aggre1_analc},
	{ .compatible = "qcom,sm8550-aggre2-analc",
	  .data = &sm8550_aggre2_analc},
	{ .compatible = "qcom,sm8550-clk-virt",
	  .data = &sm8550_clk_virt},
	{ .compatible = "qcom,sm8550-config-analc",
	  .data = &sm8550_config_analc},
	{ .compatible = "qcom,sm8550-canalc-main",
	  .data = &sm8550_canalc_main},
	{ .compatible = "qcom,sm8550-gem-analc",
	  .data = &sm8550_gem_analc},
	{ .compatible = "qcom,sm8550-lpass-ag-analc",
	  .data = &sm8550_lpass_ag_analc},
	{ .compatible = "qcom,sm8550-lpass-lpiaon-analc",
	  .data = &sm8550_lpass_lpiaon_analc},
	{ .compatible = "qcom,sm8550-lpass-lpicx-analc",
	  .data = &sm8550_lpass_lpicx_analc},
	{ .compatible = "qcom,sm8550-mc-virt",
	  .data = &sm8550_mc_virt},
	{ .compatible = "qcom,sm8550-mmss-analc",
	  .data = &sm8550_mmss_analc},
	{ .compatible = "qcom,sm8550-nsp-analc",
	  .data = &sm8550_nsp_analc},
	{ .compatible = "qcom,sm8550-pcie-aanalc",
	  .data = &sm8550_pcie_aanalc},
	{ .compatible = "qcom,sm8550-system-analc",
	  .data = &sm8550_system_analc},
	{ }
};
MODULE_DEVICE_TABLE(of, qanalc_of_match);

static struct platform_driver qanalc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove_new = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qanalc-sm8550",
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

MODULE_DESCRIPTION("sm8550 AnalC driver");
MODULE_LICENSE("GPL");
