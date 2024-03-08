// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021, Linaro Limited
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <dt-bindings/interconnect/qcom,sm8450.h>

#include "bcm-voter.h"
#include "icc-common.h"
#include "icc-rpmh.h"
#include "sm8450.h"

static struct qcom_icc_analde qhm_qspi = {
	.name = "qhm_qspi",
	.id = SM8450_MASTER_QSPI_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qup1 = {
	.name = "qhm_qup1",
	.id = SM8450_MASTER_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qnm_a1analc_cfg = {
	.name = "qnm_a1analc_cfg",
	.id = SM8450_MASTER_A1ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_SERVICE_A1ANALC },
};

static struct qcom_icc_analde xm_sdc4 = {
	.name = "xm_sdc4",
	.id = SM8450_MASTER_SDCC_4,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.id = SM8450_MASTER_UFS_MEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_usb3_0 = {
	.name = "xm_usb3_0",
	.id = SM8450_MASTER_USB3_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = SM8450_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qup0 = {
	.name = "qhm_qup0",
	.id = SM8450_MASTER_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qup2 = {
	.name = "qhm_qup2",
	.id = SM8450_MASTER_QUP_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qnm_a2analc_cfg = {
	.name = "qnm_a2analc_cfg",
	.id = SM8450_MASTER_A2ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_SERVICE_A2ANALC },
};

static struct qcom_icc_analde qxm_crypto = {
	.name = "qxm_crypto",
	.id = SM8450_MASTER_CRYPTO,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qxm_ipa = {
	.name = "qxm_ipa",
	.id = SM8450_MASTER_IPA,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qxm_sensorss_q6 = {
	.name = "qxm_sensorss_q6",
	.id = SM8450_MASTER_SENSORS_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qxm_sp = {
	.name = "qxm_sp",
	.id = SM8450_MASTER_SP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde xm_qdss_etr_0 = {
	.name = "xm_qdss_etr_0",
	.id = SM8450_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde xm_qdss_etr_1 = {
	.name = "xm_qdss_etr_1",
	.id = SM8450_MASTER_QDSS_ETR_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde xm_sdc2 = {
	.name = "xm_sdc2",
	.id = SM8450_MASTER_SDCC_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qup0_core_master = {
	.name = "qup0_core_master",
	.id = SM8450_MASTER_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_QUP_CORE_0 },
};

static struct qcom_icc_analde qup1_core_master = {
	.name = "qup1_core_master",
	.id = SM8450_MASTER_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_QUP_CORE_1 },
};

static struct qcom_icc_analde qup2_core_master = {
	.name = "qup2_core_master",
	.id = SM8450_MASTER_QUP_CORE_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_QUP_CORE_2 },
};

static struct qcom_icc_analde qnm_gemanalc_canalc = {
	.name = "qnm_gemanalc_canalc",
	.id = SM8450_MASTER_GEM_ANALC_CANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 51,
	.links = { SM8450_SLAVE_AHB2PHY_SOUTH, SM8450_SLAVE_AHB2PHY_ANALRTH,
		   SM8450_SLAVE_AOSS, SM8450_SLAVE_CAMERA_CFG,
		   SM8450_SLAVE_CLK_CTL, SM8450_SLAVE_CDSP_CFG,
		   SM8450_SLAVE_RBCPR_CX_CFG, SM8450_SLAVE_RBCPR_MMCX_CFG,
		   SM8450_SLAVE_RBCPR_MXA_CFG, SM8450_SLAVE_RBCPR_MXC_CFG,
		   SM8450_SLAVE_CRYPTO_0_CFG, SM8450_SLAVE_CX_RDPM,
		   SM8450_SLAVE_DISPLAY_CFG, SM8450_SLAVE_GFX3D_CFG,
		   SM8450_SLAVE_IMEM_CFG, SM8450_SLAVE_IPA_CFG,
		   SM8450_SLAVE_IPC_ROUTER_CFG, SM8450_SLAVE_LPASS,
		   SM8450_SLAVE_CANALC_MSS, SM8450_SLAVE_MX_RDPM,
		   SM8450_SLAVE_PCIE_0_CFG, SM8450_SLAVE_PCIE_1_CFG,
		   SM8450_SLAVE_PDM, SM8450_SLAVE_PIMEM_CFG,
		   SM8450_SLAVE_PRNG, SM8450_SLAVE_QDSS_CFG,
		   SM8450_SLAVE_QSPI_0, SM8450_SLAVE_QUP_0,
		   SM8450_SLAVE_QUP_1, SM8450_SLAVE_QUP_2,
		   SM8450_SLAVE_SDCC_2, SM8450_SLAVE_SDCC_4,
		   SM8450_SLAVE_SPSS_CFG, SM8450_SLAVE_TCSR,
		   SM8450_SLAVE_TLMM, SM8450_SLAVE_TME_CFG,
		   SM8450_SLAVE_UFS_MEM_CFG, SM8450_SLAVE_USB3_0,
		   SM8450_SLAVE_VENUS_CFG, SM8450_SLAVE_VSENSE_CTRL_CFG,
		   SM8450_SLAVE_A1ANALC_CFG, SM8450_SLAVE_A2ANALC_CFG,
		   SM8450_SLAVE_DDRSS_CFG, SM8450_SLAVE_CANALC_MANALC_CFG,
		   SM8450_SLAVE_PCIE_AANALC_CFG, SM8450_SLAVE_SANALC_CFG,
		   SM8450_SLAVE_IMEM, SM8450_SLAVE_PIMEM,
		   SM8450_SLAVE_SERVICE_CANALC, SM8450_SLAVE_QDSS_STM,
		   SM8450_SLAVE_TCU },
};

static struct qcom_icc_analde qnm_gemanalc_pcie = {
	.name = "qnm_gemanalc_pcie",
	.id = SM8450_MASTER_GEM_ANALC_PCIE_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM8450_SLAVE_PCIE_0, SM8450_SLAVE_PCIE_1 },
};

static struct qcom_icc_analde alm_gpu_tcu = {
	.name = "alm_gpu_tcu",
	.id = SM8450_MASTER_GPU_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM8450_SLAVE_GEM_ANALC_CANALC, SM8450_SLAVE_LLCC },
};

static struct qcom_icc_analde alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.id = SM8450_MASTER_SYS_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM8450_SLAVE_GEM_ANALC_CANALC, SM8450_SLAVE_LLCC },
};

static struct qcom_icc_analde chm_apps = {
	.name = "chm_apps",
	.id = SM8450_MASTER_APPSS_PROC,
	.channels = 3,
	.buswidth = 32,
	.num_links = 3,
	.links = { SM8450_SLAVE_GEM_ANALC_CANALC, SM8450_SLAVE_LLCC,
		   SM8450_SLAVE_MEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde qnm_gpu = {
	.name = "qnm_gpu",
	.id = SM8450_MASTER_GFX3D,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM8450_SLAVE_GEM_ANALC_CANALC, SM8450_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_mdsp = {
	.name = "qnm_mdsp",
	.id = SM8450_MASTER_MSS_PROC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { SM8450_SLAVE_GEM_ANALC_CANALC, SM8450_SLAVE_LLCC,
		   SM8450_SLAVE_MEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde qnm_manalc_hf = {
	.name = "qnm_manalc_hf",
	.id = SM8450_MASTER_MANALC_HF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_manalc_sf = {
	.name = "qnm_manalc_sf",
	.id = SM8450_MASTER_MANALC_SF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM8450_SLAVE_GEM_ANALC_CANALC, SM8450_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_nsp_gemanalc = {
	.name = "qnm_nsp_gemanalc",
	.id = SM8450_MASTER_COMPUTE_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM8450_SLAVE_GEM_ANALC_CANALC, SM8450_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_pcie = {
	.name = "qnm_pcie",
	.id = SM8450_MASTER_AANALC_PCIE_GEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 2,
	.links = { SM8450_SLAVE_GEM_ANALC_CANALC, SM8450_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_sanalc_gc = {
	.name = "qnm_sanalc_gc",
	.id = SM8450_MASTER_SANALC_GC_MEM_ANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_sanalc_sf = {
	.name = "qnm_sanalc_sf",
	.id = SM8450_MASTER_SANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { SM8450_SLAVE_GEM_ANALC_CANALC, SM8450_SLAVE_LLCC,
		   SM8450_SLAVE_MEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde qhm_config_analc = {
	.name = "qhm_config_analc",
	.id = SM8450_MASTER_CANALC_LPASS_AG_ANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 6,
	.links = { SM8450_SLAVE_LPASS_CORE_CFG, SM8450_SLAVE_LPASS_LPI_CFG,
		   SM8450_SLAVE_LPASS_MPU_CFG, SM8450_SLAVE_LPASS_TOP_CFG,
		   SM8450_SLAVE_SERVICES_LPASS_AML_ANALC, SM8450_SLAVE_SERVICE_LPASS_AG_ANALC },
};

static struct qcom_icc_analde qxm_lpass_dsp = {
	.name = "qxm_lpass_dsp",
	.id = SM8450_MASTER_LPASS_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 4,
	.links = { SM8450_SLAVE_LPASS_TOP_CFG, SM8450_SLAVE_LPASS_SANALC,
		   SM8450_SLAVE_SERVICES_LPASS_AML_ANALC, SM8450_SLAVE_SERVICE_LPASS_AG_ANALC },
};

static struct qcom_icc_analde llcc_mc = {
	.name = "llcc_mc",
	.id = SM8450_MASTER_LLCC,
	.channels = 4,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_EBI1 },
};

static struct qcom_icc_analde qnm_camanalc_hf = {
	.name = "qnm_camanalc_hf",
	.id = SM8450_MASTER_CAMANALC_HF,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_camanalc_icp = {
	.name = "qnm_camanalc_icp",
	.id = SM8450_MASTER_CAMANALC_ICP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_camanalc_sf = {
	.name = "qnm_camanalc_sf",
	.id = SM8450_MASTER_CAMANALC_SF,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_mdp = {
	.name = "qnm_mdp",
	.id = SM8450_MASTER_MDP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_manalc_cfg = {
	.name = "qnm_manalc_cfg",
	.id = SM8450_MASTER_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_SERVICE_MANALC },
};

static struct qcom_icc_analde qnm_rot = {
	.name = "qnm_rot",
	.id = SM8450_MASTER_ROTATOR,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_vapss_hcp = {
	.name = "qnm_vapss_hcp",
	.id = SM8450_MASTER_CDSP_HCP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_video = {
	.name = "qnm_video",
	.id = SM8450_MASTER_VIDEO,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_video_cv_cpu = {
	.name = "qnm_video_cv_cpu",
	.id = SM8450_MASTER_VIDEO_CV_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_video_cvp = {
	.name = "qnm_video_cvp",
	.id = SM8450_MASTER_VIDEO_PROC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_video_v_cpu = {
	.name = "qnm_video_v_cpu",
	.id = SM8450_MASTER_VIDEO_V_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qhm_nsp_analc_config = {
	.name = "qhm_nsp_analc_config",
	.id = SM8450_MASTER_CDSP_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_SERVICE_NSP_ANALC },
};

static struct qcom_icc_analde qxm_nsp = {
	.name = "qxm_nsp",
	.id = SM8450_MASTER_CDSP_PROC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_CDSP_MEM_ANALC },
};

static struct qcom_icc_analde qnm_pcie_aanalc_cfg = {
	.name = "qnm_pcie_aanalc_cfg",
	.id = SM8450_MASTER_PCIE_AANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_SERVICE_PCIE_AANALC },
};

static struct qcom_icc_analde xm_pcie3_0 = {
	.name = "xm_pcie3_0",
	.id = SM8450_MASTER_PCIE_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde xm_pcie3_1 = {
	.name = "xm_pcie3_1",
	.id = SM8450_MASTER_PCIE_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde qhm_gic = {
	.name = "qhm_gic",
	.id = SM8450_MASTER_GIC_AHB,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qnm_aggre1_analc = {
	.name = "qnm_aggre1_analc",
	.id = SM8450_MASTER_A1ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qnm_aggre2_analc = {
	.name = "qnm_aggre2_analc",
	.id = SM8450_MASTER_A2ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qnm_lpass_analc = {
	.name = "qnm_lpass_analc",
	.id = SM8450_MASTER_LPASS_AANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qnm_sanalc_cfg = {
	.name = "qnm_sanalc_cfg",
	.id = SM8450_MASTER_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_SERVICE_SANALC },
};

static struct qcom_icc_analde qxm_pimem = {
	.name = "qxm_pimem",
	.id = SM8450_MASTER_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_SANALC_GEM_ANALC_GC },
};

static struct qcom_icc_analde xm_gic = {
	.name = "xm_gic",
	.id = SM8450_MASTER_GIC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_SANALC_GEM_ANALC_GC },
};

static struct qcom_icc_analde qnm_manalc_hf_disp = {
	.name = "qnm_manalc_hf_disp",
	.id = SM8450_MASTER_MANALC_HF_MEM_ANALC_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_LLCC_DISP },
};

static struct qcom_icc_analde qnm_manalc_sf_disp = {
	.name = "qnm_manalc_sf_disp",
	.id = SM8450_MASTER_MANALC_SF_MEM_ANALC_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_LLCC_DISP },
};

static struct qcom_icc_analde qnm_pcie_disp = {
	.name = "qnm_pcie_disp",
	.id = SM8450_MASTER_AANALC_PCIE_GEM_ANALC_DISP,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_SLAVE_LLCC_DISP },
};

static struct qcom_icc_analde llcc_mc_disp = {
	.name = "llcc_mc_disp",
	.id = SM8450_MASTER_LLCC_DISP,
	.channels = 4,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_EBI1_DISP },
};

static struct qcom_icc_analde qnm_mdp_disp = {
	.name = "qnm_mdp_disp",
	.id = SM8450_MASTER_MDP_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_MANALC_HF_MEM_ANALC_DISP },
};

static struct qcom_icc_analde qnm_rot_disp = {
	.name = "qnm_rot_disp",
	.id = SM8450_MASTER_ROTATOR_DISP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_MANALC_SF_MEM_ANALC_DISP },
};

static struct qcom_icc_analde qns_a1analc_sanalc = {
	.name = "qns_a1analc_sanalc",
	.id = SM8450_SLAVE_A1ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_MASTER_A1ANALC_SANALC },
};

static struct qcom_icc_analde srvc_aggre1_analc = {
	.name = "srvc_aggre1_analc",
	.id = SM8450_SLAVE_SERVICE_A1ANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_a2analc_sanalc = {
	.name = "qns_a2analc_sanalc",
	.id = SM8450_SLAVE_A2ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_MASTER_A2ANALC_SANALC },
};

static struct qcom_icc_analde srvc_aggre2_analc = {
	.name = "srvc_aggre2_analc",
	.id = SM8450_SLAVE_SERVICE_A2ANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qup0_core_slave = {
	.name = "qup0_core_slave",
	.id = SM8450_SLAVE_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qup1_core_slave = {
	.name = "qup1_core_slave",
	.id = SM8450_SLAVE_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qup2_core_slave = {
	.name = "qup2_core_slave",
	.id = SM8450_SLAVE_QUP_CORE_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ahb2phy0 = {
	.name = "qhs_ahb2phy0",
	.id = SM8450_SLAVE_AHB2PHY_SOUTH,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ahb2phy1 = {
	.name = "qhs_ahb2phy1",
	.id = SM8450_SLAVE_AHB2PHY_ANALRTH,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_aoss = {
	.name = "qhs_aoss",
	.id = SM8450_SLAVE_AOSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_camera_cfg = {
	.name = "qhs_camera_cfg",
	.id = SM8450_SLAVE_CAMERA_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SM8450_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_compute_cfg = {
	.name = "qhs_compute_cfg",
	.id = SM8450_SLAVE_CDSP_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { MASTER_CDSP_ANALC_CFG },
};

static struct qcom_icc_analde qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = SM8450_SLAVE_RBCPR_CX_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_cpr_mmcx = {
	.name = "qhs_cpr_mmcx",
	.id = SM8450_SLAVE_RBCPR_MMCX_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_cpr_mxa = {
	.name = "qhs_cpr_mxa",
	.id = SM8450_SLAVE_RBCPR_MXA_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_cpr_mxc = {
	.name = "qhs_cpr_mxc",
	.id = SM8450_SLAVE_RBCPR_MXC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = SM8450_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_cx_rdpm = {
	.name = "qhs_cx_rdpm",
	.id = SM8450_SLAVE_CX_RDPM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_display_cfg = {
	.name = "qhs_display_cfg",
	.id = SM8450_SLAVE_DISPLAY_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.id = SM8450_SLAVE_GFX3D_CFG,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SM8450_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ipa = {
	.name = "qhs_ipa",
	.id = SM8450_SLAVE_IPA_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ipc_router = {
	.name = "qhs_ipc_router",
	.id = SM8450_SLAVE_IPC_ROUTER_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_lpass_cfg = {
	.name = "qhs_lpass_cfg",
	.id = SM8450_SLAVE_LPASS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { MASTER_CANALC_LPASS_AG_ANALC },
};

static struct qcom_icc_analde qhs_mss_cfg = {
	.name = "qhs_mss_cfg",
	.id = SM8450_SLAVE_CANALC_MSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_mx_rdpm = {
	.name = "qhs_mx_rdpm",
	.id = SM8450_SLAVE_MX_RDPM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pcie0_cfg = {
	.name = "qhs_pcie0_cfg",
	.id = SM8450_SLAVE_PCIE_0_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pcie1_cfg = {
	.name = "qhs_pcie1_cfg",
	.id = SM8450_SLAVE_PCIE_1_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pdm = {
	.name = "qhs_pdm",
	.id = SM8450_SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = SM8450_SLAVE_PIMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_prng = {
	.name = "qhs_prng",
	.id = SM8450_SLAVE_PRNG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SM8450_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qspi = {
	.name = "qhs_qspi",
	.id = SM8450_SLAVE_QSPI_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qup0 = {
	.name = "qhs_qup0",
	.id = SM8450_SLAVE_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qup1 = {
	.name = "qhs_qup1",
	.id = SM8450_SLAVE_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qup2 = {
	.name = "qhs_qup2",
	.id = SM8450_SLAVE_QUP_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = SM8450_SLAVE_SDCC_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_sdc4 = {
	.name = "qhs_sdc4",
	.id = SM8450_SLAVE_SDCC_4,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_spss_cfg = {
	.name = "qhs_spss_cfg",
	.id = SM8450_SLAVE_SPSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SM8450_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_tlmm = {
	.name = "qhs_tlmm",
	.id = SM8450_SLAVE_TLMM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_tme_cfg = {
	.name = "qhs_tme_cfg",
	.id = SM8450_SLAVE_TME_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.id = SM8450_SLAVE_UFS_MEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_usb3_0 = {
	.name = "qhs_usb3_0",
	.id = SM8450_SLAVE_USB3_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = SM8450_SLAVE_VENUS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.id = SM8450_SLAVE_VSENSE_CTRL_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_a1_analc_cfg = {
	.name = "qns_a1_analc_cfg",
	.id = SM8450_SLAVE_A1ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_MASTER_A1ANALC_CFG },
};

static struct qcom_icc_analde qns_a2_analc_cfg = {
	.name = "qns_a2_analc_cfg",
	.id = SM8450_SLAVE_A2ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_MASTER_A2ANALC_CFG },
};

static struct qcom_icc_analde qns_ddrss_cfg = {
	.name = "qns_ddrss_cfg",
	.id = SM8450_SLAVE_DDRSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	//FIXME where is link
};

static struct qcom_icc_analde qns_manalc_cfg = {
	.name = "qns_manalc_cfg",
	.id = SM8450_SLAVE_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_MASTER_CANALC_MANALC_CFG },
};

static struct qcom_icc_analde qns_pcie_aanalc_cfg = {
	.name = "qns_pcie_aanalc_cfg",
	.id = SM8450_SLAVE_PCIE_AANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_MASTER_PCIE_AANALC_CFG },
};

static struct qcom_icc_analde qns_sanalc_cfg = {
	.name = "qns_sanalc_cfg",
	.id = SM8450_SLAVE_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_MASTER_SANALC_CFG },
};

static struct qcom_icc_analde qxs_imem = {
	.name = "qxs_imem",
	.id = SM8450_SLAVE_IMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde qxs_pimem = {
	.name = "qxs_pimem",
	.id = SM8450_SLAVE_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde srvc_canalc = {
	.name = "srvc_canalc",
	.id = SM8450_SLAVE_SERVICE_CANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde xs_pcie_0 = {
	.name = "xs_pcie_0",
	.id = SM8450_SLAVE_PCIE_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde xs_pcie_1 = {
	.name = "xs_pcie_1",
	.id = SM8450_SLAVE_PCIE_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SM8450_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SM8450_SLAVE_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde qns_gem_analc_canalc = {
	.name = "qns_gem_analc_canalc",
	.id = SM8450_SLAVE_GEM_ANALC_CANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_MASTER_GEM_ANALC_CANALC },
};

static struct qcom_icc_analde qns_llcc = {
	.name = "qns_llcc",
	.id = SM8450_SLAVE_LLCC,
	.channels = 4,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_MASTER_LLCC },
};

static struct qcom_icc_analde qns_pcie = {
	.name = "qns_pcie",
	.id = SM8450_SLAVE_MEM_ANALC_PCIE_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_MASTER_GEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde qhs_lpass_core = {
	.name = "qhs_lpass_core",
	.id = SM8450_SLAVE_LPASS_CORE_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_lpass_lpi = {
	.name = "qhs_lpass_lpi",
	.id = SM8450_SLAVE_LPASS_LPI_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_lpass_mpu = {
	.name = "qhs_lpass_mpu",
	.id = SM8450_SLAVE_LPASS_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_lpass_top = {
	.name = "qhs_lpass_top",
	.id = SM8450_SLAVE_LPASS_TOP_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_sysanalc = {
	.name = "qns_sysanalc",
	.id = SM8450_SLAVE_LPASS_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_MASTER_LPASS_AANALC },
};

static struct qcom_icc_analde srvc_niu_aml_analc = {
	.name = "srvc_niu_aml_analc",
	.id = SM8450_SLAVE_SERVICES_LPASS_AML_ANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde srvc_niu_lpass_aganalc = {
	.name = "srvc_niu_lpass_aganalc",
	.id = SM8450_SLAVE_SERVICE_LPASS_AG_ANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde ebi = {
	.name = "ebi",
	.id = SM8450_SLAVE_EBI1,
	.channels = 4,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_mem_analc_hf = {
	.name = "qns_mem_analc_hf",
	.id = SM8450_SLAVE_MANALC_HF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_MASTER_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qns_mem_analc_sf = {
	.name = "qns_mem_analc_sf",
	.id = SM8450_SLAVE_MANALC_SF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_MASTER_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde srvc_manalc = {
	.name = "srvc_manalc",
	.id = SM8450_SLAVE_SERVICE_MANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_nsp_gemanalc = {
	.name = "qns_nsp_gemanalc",
	.id = SM8450_SLAVE_CDSP_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_MASTER_COMPUTE_ANALC },
};

static struct qcom_icc_analde service_nsp_analc = {
	.name = "service_nsp_analc",
	.id = SM8450_SLAVE_SERVICE_NSP_ANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_pcie_mem_analc = {
	.name = "qns_pcie_mem_analc",
	.id = SM8450_SLAVE_AANALC_PCIE_GEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_MASTER_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde srvc_pcie_aggre_analc = {
	.name = "srvc_pcie_aggre_analc",
	.id = SM8450_SLAVE_SERVICE_PCIE_AANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_gemanalc_gc = {
	.name = "qns_gemanalc_gc",
	.id = SM8450_SLAVE_SANALC_GEM_ANALC_GC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_MASTER_SANALC_GC_MEM_ANALC },
};

static struct qcom_icc_analde qns_gemanalc_sf = {
	.name = "qns_gemanalc_sf",
	.id = SM8450_SLAVE_SANALC_GEM_ANALC_SF,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_MASTER_SANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde srvc_sanalc = {
	.name = "srvc_sanalc",
	.id = SM8450_SLAVE_SERVICE_SANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_llcc_disp = {
	.name = "qns_llcc_disp",
	.id = SM8450_SLAVE_LLCC_DISP,
	.channels = 4,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_MASTER_LLCC_DISP },
};

static struct qcom_icc_analde ebi_disp = {
	.name = "ebi_disp",
	.id = SM8450_SLAVE_EBI1_DISP,
	.channels = 4,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_mem_analc_hf_disp = {
	.name = "qns_mem_analc_hf_disp",
	.id = SM8450_SLAVE_MANALC_HF_MEM_ANALC_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_MASTER_MANALC_HF_MEM_ANALC_DISP },
};

static struct qcom_icc_analde qns_mem_analc_sf_disp = {
	.name = "qns_mem_analc_sf_disp",
	.id = SM8450_SLAVE_MANALC_SF_MEM_ANALC_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_MASTER_MANALC_SF_MEM_ANALC_DISP },
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
	.num_analdes = 55,
	.analdes = { &qnm_gemanalc_canalc, &qnm_gemanalc_pcie,
		   &qhs_ahb2phy0, &qhs_ahb2phy1,
		   &qhs_aoss, &qhs_camera_cfg,
		   &qhs_clk_ctl, &qhs_compute_cfg,
		   &qhs_cpr_cx, &qhs_cpr_mmcx,
		   &qhs_cpr_mxa, &qhs_cpr_mxc,
		   &qhs_crypto0_cfg, &qhs_cx_rdpm,
		   &qhs_display_cfg, &qhs_gpuss_cfg,
		   &qhs_imem_cfg, &qhs_ipa,
		   &qhs_ipc_router, &qhs_lpass_cfg,
		   &qhs_mss_cfg, &qhs_mx_rdpm,
		   &qhs_pcie0_cfg, &qhs_pcie1_cfg,
		   &qhs_pdm, &qhs_pimem_cfg,
		   &qhs_prng, &qhs_qdss_cfg,
		   &qhs_qspi, &qhs_qup0,
		   &qhs_qup1, &qhs_qup2,
		   &qhs_sdc2, &qhs_sdc4,
		   &qhs_spss_cfg, &qhs_tcsr,
		   &qhs_tlmm, &qhs_tme_cfg,
		   &qhs_ufs_mem_cfg, &qhs_usb3_0,
		   &qhs_venus_cfg, &qhs_vsense_ctrl_cfg,
		   &qns_a1_analc_cfg, &qns_a2_analc_cfg,
		   &qns_ddrss_cfg, &qns_manalc_cfg,
		   &qns_pcie_aanalc_cfg, &qns_sanalc_cfg,
		   &qxs_imem, &qxs_pimem,
		   &srvc_canalc, &xs_pcie_0,
		   &xs_pcie_1, &xs_qdss_stm,
		   &xs_sys_tcu_cfg },
};

static struct qcom_icc_bcm bcm_co0 = {
	.name = "CO0",
	.enable_mask = 0x1,
	.num_analdes = 2,
	.analdes = { &qxm_nsp, &qns_nsp_gemanalc },
};

static struct qcom_icc_bcm bcm_mc0 = {
	.name = "MC0",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &ebi },
};

static struct qcom_icc_bcm bcm_mm0 = {
	.name = "MM0",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &qns_mem_analc_hf },
};

static struct qcom_icc_bcm bcm_mm1 = {
	.name = "MM1",
	.enable_mask = 0x1,
	.num_analdes = 12,
	.analdes = { &qnm_camanalc_hf, &qnm_camanalc_icp,
		   &qnm_camanalc_sf, &qnm_mdp,
		   &qnm_manalc_cfg, &qnm_rot,
		   &qnm_vapss_hcp, &qnm_video,
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
	.num_analdes = 7,
	.analdes = { &alm_gpu_tcu, &alm_sys_tcu,
		   &qnm_nsp_gemanalc, &qnm_pcie,
		   &qnm_sanalc_gc, &qns_gem_analc_canalc,
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
	.num_analdes = 4,
	.analdes = { &qhm_gic, &qxm_pimem,
		   &xm_gic, &qns_gemanalc_gc },
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
	.analdes = { &qnm_lpass_analc },
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

static struct qcom_icc_bcm bcm_mm1_disp = {
	.name = "MM1",
	.enable_mask = 0x1,
	.num_analdes = 3,
	.analdes = { &qnm_mdp_disp, &qnm_rot_disp,
		   &qns_mem_analc_sf_disp },
};

static struct qcom_icc_bcm bcm_sh0_disp = {
	.name = "SH0",
	.num_analdes = 1,
	.analdes = { &qns_llcc_disp },
};

static struct qcom_icc_bcm bcm_sh1_disp = {
	.name = "SH1",
	.enable_mask = 0x1,
	.num_analdes = 1,
	.analdes = { &qnm_pcie_disp },
};

static struct qcom_icc_bcm * const aggre1_analc_bcms[] = {
};

static struct qcom_icc_analde * const aggre1_analc_analdes[] = {
	[MASTER_QSPI_0] = &qhm_qspi,
	[MASTER_QUP_1] = &qhm_qup1,
	[MASTER_A1ANALC_CFG] = &qnm_a1analc_cfg,
	[MASTER_SDCC_4] = &xm_sdc4,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_USB3_0] = &xm_usb3_0,
	[SLAVE_A1ANALC_SANALC] = &qns_a1analc_sanalc,
	[SLAVE_SERVICE_A1ANALC] = &srvc_aggre1_analc,
};

static const struct qcom_icc_desc sm8450_aggre1_analc = {
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
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_QUP_2] = &qhm_qup2,
	[MASTER_A2ANALC_CFG] = &qnm_a2analc_cfg,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_SENSORS_PROC] = &qxm_sensorss_q6,
	[MASTER_SP] = &qxm_sp,
	[MASTER_QDSS_ETR] = &xm_qdss_etr_0,
	[MASTER_QDSS_ETR_1] = &xm_qdss_etr_1,
	[MASTER_SDCC_2] = &xm_sdc2,
	[SLAVE_A2ANALC_SANALC] = &qns_a2analc_sanalc,
	[SLAVE_SERVICE_A2ANALC] = &srvc_aggre2_analc,
};

static const struct qcom_icc_desc sm8450_aggre2_analc = {
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

static const struct qcom_icc_desc sm8450_clk_virt = {
	.analdes = clk_virt_analdes,
	.num_analdes = ARRAY_SIZE(clk_virt_analdes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
};

static struct qcom_icc_bcm * const config_analc_bcms[] = {
	&bcm_cn0,
};

static struct qcom_icc_analde * const config_analc_analdes[] = {
	[MASTER_GEM_ANALC_CANALC] = &qnm_gemanalc_canalc,
	[MASTER_GEM_ANALC_PCIE_SANALC] = &qnm_gemanalc_pcie,
	[SLAVE_AHB2PHY_SOUTH] = &qhs_ahb2phy0,
	[SLAVE_AHB2PHY_ANALRTH] = &qhs_ahb2phy1,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CDSP_CFG] = &qhs_compute_cfg,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MMCX_CFG] = &qhs_cpr_mmcx,
	[SLAVE_RBCPR_MXA_CFG] = &qhs_cpr_mxa,
	[SLAVE_RBCPR_MXC_CFG] = &qhs_cpr_mxc,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_CX_RDPM] = &qhs_cx_rdpm,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_GFX3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_IPC_ROUTER_CFG] = &qhs_ipc_router,
	[SLAVE_LPASS] = &qhs_lpass_cfg,
	[SLAVE_CANALC_MSS] = &qhs_mss_cfg,
	[SLAVE_MX_RDPM] = &qhs_mx_rdpm,
	[SLAVE_PCIE_0_CFG] = &qhs_pcie0_cfg,
	[SLAVE_PCIE_1_CFG] = &qhs_pcie1_cfg,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QSPI_0] = &qhs_qspi,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_QUP_2] = &qhs_qup2,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SDCC_4] = &qhs_sdc4,
	[SLAVE_SPSS_CFG] = &qhs_spss_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_TME_CFG] = &qhs_tme_cfg,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB3_0] = &qhs_usb3_0,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_A1ANALC_CFG] = &qns_a1_analc_cfg,
	[SLAVE_A2ANALC_CFG] = &qns_a2_analc_cfg,
	[SLAVE_DDRSS_CFG] = &qns_ddrss_cfg,
	[SLAVE_CANALC_MANALC_CFG] = &qns_manalc_cfg,
	[SLAVE_PCIE_AANALC_CFG] = &qns_pcie_aanalc_cfg,
	[SLAVE_SANALC_CFG] = &qns_sanalc_cfg,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SERVICE_CANALC] = &srvc_canalc,
	[SLAVE_PCIE_0] = &xs_pcie_0,
	[SLAVE_PCIE_1] = &xs_pcie_1,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sm8450_config_analc = {
	.analdes = config_analc_analdes,
	.num_analdes = ARRAY_SIZE(config_analc_analdes),
	.bcms = config_analc_bcms,
	.num_bcms = ARRAY_SIZE(config_analc_bcms),
};

static struct qcom_icc_bcm * const gem_analc_bcms[] = {
	&bcm_sh0,
	&bcm_sh1,
	&bcm_sh0_disp,
	&bcm_sh1_disp,
};

static struct qcom_icc_analde * const gem_analc_analdes[] = {
	[MASTER_GPU_TCU] = &alm_gpu_tcu,
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_APPSS_PROC] = &chm_apps,
	[MASTER_GFX3D] = &qnm_gpu,
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
	[MASTER_MANALC_SF_MEM_ANALC_DISP] = &qnm_manalc_sf_disp,
	[MASTER_AANALC_PCIE_GEM_ANALC_DISP] = &qnm_pcie_disp,
	[SLAVE_LLCC_DISP] = &qns_llcc_disp,
};

static const struct qcom_icc_desc sm8450_gem_analc = {
	.analdes = gem_analc_analdes,
	.num_analdes = ARRAY_SIZE(gem_analc_analdes),
	.bcms = gem_analc_bcms,
	.num_bcms = ARRAY_SIZE(gem_analc_bcms),
};

static struct qcom_icc_bcm * const lpass_ag_analc_bcms[] = {
};

static struct qcom_icc_analde * const lpass_ag_analc_analdes[] = {
	[MASTER_CANALC_LPASS_AG_ANALC] = &qhm_config_analc,
	[MASTER_LPASS_PROC] = &qxm_lpass_dsp,
	[SLAVE_LPASS_CORE_CFG] = &qhs_lpass_core,
	[SLAVE_LPASS_LPI_CFG] = &qhs_lpass_lpi,
	[SLAVE_LPASS_MPU_CFG] = &qhs_lpass_mpu,
	[SLAVE_LPASS_TOP_CFG] = &qhs_lpass_top,
	[SLAVE_LPASS_SANALC] = &qns_sysanalc,
	[SLAVE_SERVICES_LPASS_AML_ANALC] = &srvc_niu_aml_analc,
	[SLAVE_SERVICE_LPASS_AG_ANALC] = &srvc_niu_lpass_aganalc,
};

static const struct qcom_icc_desc sm8450_lpass_ag_analc = {
	.analdes = lpass_ag_analc_analdes,
	.num_analdes = ARRAY_SIZE(lpass_ag_analc_analdes),
	.bcms = lpass_ag_analc_bcms,
	.num_bcms = ARRAY_SIZE(lpass_ag_analc_bcms),
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
	&bcm_acv_disp,
	&bcm_mc0_disp,
};

static struct qcom_icc_analde * const mc_virt_analdes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
	[MASTER_LLCC_DISP] = &llcc_mc_disp,
	[SLAVE_EBI1_DISP] = &ebi_disp,
};

static const struct qcom_icc_desc sm8450_mc_virt = {
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
	[MASTER_CAMANALC_HF] = &qnm_camanalc_hf,
	[MASTER_CAMANALC_ICP] = &qnm_camanalc_icp,
	[MASTER_CAMANALC_SF] = &qnm_camanalc_sf,
	[MASTER_MDP] = &qnm_mdp,
	[MASTER_CANALC_MANALC_CFG] = &qnm_manalc_cfg,
	[MASTER_ROTATOR] = &qnm_rot,
	[MASTER_CDSP_HCP] = &qnm_vapss_hcp,
	[MASTER_VIDEO] = &qnm_video,
	[MASTER_VIDEO_CV_PROC] = &qnm_video_cv_cpu,
	[MASTER_VIDEO_PROC] = &qnm_video_cvp,
	[MASTER_VIDEO_V_PROC] = &qnm_video_v_cpu,
	[SLAVE_MANALC_HF_MEM_ANALC] = &qns_mem_analc_hf,
	[SLAVE_MANALC_SF_MEM_ANALC] = &qns_mem_analc_sf,
	[SLAVE_SERVICE_MANALC] = &srvc_manalc,
	[MASTER_MDP_DISP] = &qnm_mdp_disp,
	[MASTER_ROTATOR_DISP] = &qnm_rot_disp,
	[SLAVE_MANALC_HF_MEM_ANALC_DISP] = &qns_mem_analc_hf_disp,
	[SLAVE_MANALC_SF_MEM_ANALC_DISP] = &qns_mem_analc_sf_disp,
};

static const struct qcom_icc_desc sm8450_mmss_analc = {
	.analdes = mmss_analc_analdes,
	.num_analdes = ARRAY_SIZE(mmss_analc_analdes),
	.bcms = mmss_analc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_analc_bcms),
};

static struct qcom_icc_bcm * const nsp_analc_bcms[] = {
	&bcm_co0,
};

static struct qcom_icc_analde * const nsp_analc_analdes[] = {
	[MASTER_CDSP_ANALC_CFG] = &qhm_nsp_analc_config,
	[MASTER_CDSP_PROC] = &qxm_nsp,
	[SLAVE_CDSP_MEM_ANALC] = &qns_nsp_gemanalc,
	[SLAVE_SERVICE_NSP_ANALC] = &service_nsp_analc,
};

static const struct qcom_icc_desc sm8450_nsp_analc = {
	.analdes = nsp_analc_analdes,
	.num_analdes = ARRAY_SIZE(nsp_analc_analdes),
	.bcms = nsp_analc_bcms,
	.num_bcms = ARRAY_SIZE(nsp_analc_bcms),
};

static struct qcom_icc_bcm * const pcie_aanalc_bcms[] = {
	&bcm_sn7,
};

static struct qcom_icc_analde * const pcie_aanalc_analdes[] = {
	[MASTER_PCIE_AANALC_CFG] = &qnm_pcie_aanalc_cfg,
	[MASTER_PCIE_0] = &xm_pcie3_0,
	[MASTER_PCIE_1] = &xm_pcie3_1,
	[SLAVE_AANALC_PCIE_GEM_ANALC] = &qns_pcie_mem_analc,
	[SLAVE_SERVICE_PCIE_AANALC] = &srvc_pcie_aggre_analc,
};

static const struct qcom_icc_desc sm8450_pcie_aanalc = {
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
	&bcm_sn4,
};

static struct qcom_icc_analde * const system_analc_analdes[] = {
	[MASTER_GIC_AHB] = &qhm_gic,
	[MASTER_A1ANALC_SANALC] = &qnm_aggre1_analc,
	[MASTER_A2ANALC_SANALC] = &qnm_aggre2_analc,
	[MASTER_LPASS_AANALC] = &qnm_lpass_analc,
	[MASTER_SANALC_CFG] = &qnm_sanalc_cfg,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_GIC] = &xm_gic,
	[SLAVE_SANALC_GEM_ANALC_GC] = &qns_gemanalc_gc,
	[SLAVE_SANALC_GEM_ANALC_SF] = &qns_gemanalc_sf,
	[SLAVE_SERVICE_SANALC] = &srvc_sanalc,
};

static const struct qcom_icc_desc sm8450_system_analc = {
	.analdes = system_analc_analdes,
	.num_analdes = ARRAY_SIZE(system_analc_analdes),
	.bcms = system_analc_bcms,
	.num_bcms = ARRAY_SIZE(system_analc_bcms),
};

static const struct of_device_id qanalc_of_match[] = {
	{ .compatible = "qcom,sm8450-aggre1-analc",
	  .data = &sm8450_aggre1_analc},
	{ .compatible = "qcom,sm8450-aggre2-analc",
	  .data = &sm8450_aggre2_analc},
	{ .compatible = "qcom,sm8450-clk-virt",
	  .data = &sm8450_clk_virt},
	{ .compatible = "qcom,sm8450-config-analc",
	  .data = &sm8450_config_analc},
	{ .compatible = "qcom,sm8450-gem-analc",
	  .data = &sm8450_gem_analc},
	{ .compatible = "qcom,sm8450-lpass-ag-analc",
	  .data = &sm8450_lpass_ag_analc},
	{ .compatible = "qcom,sm8450-mc-virt",
	  .data = &sm8450_mc_virt},
	{ .compatible = "qcom,sm8450-mmss-analc",
	  .data = &sm8450_mmss_analc},
	{ .compatible = "qcom,sm8450-nsp-analc",
	  .data = &sm8450_nsp_analc},
	{ .compatible = "qcom,sm8450-pcie-aanalc",
	  .data = &sm8450_pcie_aanalc},
	{ .compatible = "qcom,sm8450-system-analc",
	  .data = &sm8450_system_analc},
	{ }
};
MODULE_DEVICE_TABLE(of, qanalc_of_match);

static struct platform_driver qanalc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove_new = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qanalc-sm8450",
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

MODULE_DESCRIPTION("sm8450 AnalC driver");
MODULE_LICENSE("GPL v2");
