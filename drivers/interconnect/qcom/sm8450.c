// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021, Linaro Limited
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <dt-bindings/interconnect/qcom,sm8450.h>

#include "bcm-voter.h"
#include "icc-common.h"
#include "icc-rpmh.h"
#include "sm8450.h"

static struct qcom_icc_node qhm_qspi = {
	.name = "qhm_qspi",
	.id = SM8450_MASTER_QSPI_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node qhm_qup1 = {
	.name = "qhm_qup1",
	.id = SM8450_MASTER_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node qnm_a1noc_cfg = {
	.name = "qnm_a1noc_cfg",
	.id = SM8450_MASTER_A1NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_SERVICE_A1NOC },
};

static struct qcom_icc_node xm_sdc4 = {
	.name = "xm_sdc4",
	.id = SM8450_MASTER_SDCC_4,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.id = SM8450_MASTER_UFS_MEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node xm_usb3_0 = {
	.name = "xm_usb3_0",
	.id = SM8450_MASTER_USB3_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = SM8450_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qhm_qup0 = {
	.name = "qhm_qup0",
	.id = SM8450_MASTER_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qhm_qup2 = {
	.name = "qhm_qup2",
	.id = SM8450_MASTER_QUP_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qnm_a2noc_cfg = {
	.name = "qnm_a2noc_cfg",
	.id = SM8450_MASTER_A2NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_SERVICE_A2NOC },
};

static struct qcom_icc_node qxm_crypto = {
	.name = "qxm_crypto",
	.id = SM8450_MASTER_CRYPTO,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qxm_ipa = {
	.name = "qxm_ipa",
	.id = SM8450_MASTER_IPA,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qxm_sensorss_q6 = {
	.name = "qxm_sensorss_q6",
	.id = SM8450_MASTER_SENSORS_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qxm_sp = {
	.name = "qxm_sp",
	.id = SM8450_MASTER_SP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node xm_qdss_etr_0 = {
	.name = "xm_qdss_etr_0",
	.id = SM8450_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node xm_qdss_etr_1 = {
	.name = "xm_qdss_etr_1",
	.id = SM8450_MASTER_QDSS_ETR_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node xm_sdc2 = {
	.name = "xm_sdc2",
	.id = SM8450_MASTER_SDCC_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qup0_core_master = {
	.name = "qup0_core_master",
	.id = SM8450_MASTER_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_QUP_CORE_0 },
};

static struct qcom_icc_node qup1_core_master = {
	.name = "qup1_core_master",
	.id = SM8450_MASTER_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_QUP_CORE_1 },
};

static struct qcom_icc_node qup2_core_master = {
	.name = "qup2_core_master",
	.id = SM8450_MASTER_QUP_CORE_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_QUP_CORE_2 },
};

static struct qcom_icc_node qnm_gemnoc_cnoc = {
	.name = "qnm_gemnoc_cnoc",
	.id = SM8450_MASTER_GEM_NOC_CNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 51,
	.links = { SM8450_SLAVE_AHB2PHY_SOUTH, SM8450_SLAVE_AHB2PHY_NORTH,
		   SM8450_SLAVE_AOSS, SM8450_SLAVE_CAMERA_CFG,
		   SM8450_SLAVE_CLK_CTL, SM8450_SLAVE_CDSP_CFG,
		   SM8450_SLAVE_RBCPR_CX_CFG, SM8450_SLAVE_RBCPR_MMCX_CFG,
		   SM8450_SLAVE_RBCPR_MXA_CFG, SM8450_SLAVE_RBCPR_MXC_CFG,
		   SM8450_SLAVE_CRYPTO_0_CFG, SM8450_SLAVE_CX_RDPM,
		   SM8450_SLAVE_DISPLAY_CFG, SM8450_SLAVE_GFX3D_CFG,
		   SM8450_SLAVE_IMEM_CFG, SM8450_SLAVE_IPA_CFG,
		   SM8450_SLAVE_IPC_ROUTER_CFG, SM8450_SLAVE_LPASS,
		   SM8450_SLAVE_CNOC_MSS, SM8450_SLAVE_MX_RDPM,
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
		   SM8450_SLAVE_A1NOC_CFG, SM8450_SLAVE_A2NOC_CFG,
		   SM8450_SLAVE_DDRSS_CFG, SM8450_SLAVE_CNOC_MNOC_CFG,
		   SM8450_SLAVE_PCIE_ANOC_CFG, SM8450_SLAVE_SNOC_CFG,
		   SM8450_SLAVE_IMEM, SM8450_SLAVE_PIMEM,
		   SM8450_SLAVE_SERVICE_CNOC, SM8450_SLAVE_QDSS_STM,
		   SM8450_SLAVE_TCU },
};

static struct qcom_icc_node qnm_gemnoc_pcie = {
	.name = "qnm_gemnoc_pcie",
	.id = SM8450_MASTER_GEM_NOC_PCIE_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM8450_SLAVE_PCIE_0, SM8450_SLAVE_PCIE_1 },
};

static struct qcom_icc_node alm_gpu_tcu = {
	.name = "alm_gpu_tcu",
	.id = SM8450_MASTER_GPU_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM8450_SLAVE_GEM_NOC_CNOC, SM8450_SLAVE_LLCC },
};

static struct qcom_icc_node alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.id = SM8450_MASTER_SYS_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM8450_SLAVE_GEM_NOC_CNOC, SM8450_SLAVE_LLCC },
};

static struct qcom_icc_node chm_apps = {
	.name = "chm_apps",
	.id = SM8450_MASTER_APPSS_PROC,
	.channels = 3,
	.buswidth = 32,
	.num_links = 3,
	.links = { SM8450_SLAVE_GEM_NOC_CNOC, SM8450_SLAVE_LLCC,
		   SM8450_SLAVE_MEM_NOC_PCIE_SNOC },
};

static struct qcom_icc_node qnm_gpu = {
	.name = "qnm_gpu",
	.id = SM8450_MASTER_GFX3D,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM8450_SLAVE_GEM_NOC_CNOC, SM8450_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_mdsp = {
	.name = "qnm_mdsp",
	.id = SM8450_MASTER_MSS_PROC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { SM8450_SLAVE_GEM_NOC_CNOC, SM8450_SLAVE_LLCC,
		   SM8450_SLAVE_MEM_NOC_PCIE_SNOC },
};

static struct qcom_icc_node qnm_mnoc_hf = {
	.name = "qnm_mnoc_hf",
	.id = SM8450_MASTER_MNOC_HF_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_mnoc_sf = {
	.name = "qnm_mnoc_sf",
	.id = SM8450_MASTER_MNOC_SF_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM8450_SLAVE_GEM_NOC_CNOC, SM8450_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_nsp_gemnoc = {
	.name = "qnm_nsp_gemnoc",
	.id = SM8450_MASTER_COMPUTE_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM8450_SLAVE_GEM_NOC_CNOC, SM8450_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_pcie = {
	.name = "qnm_pcie",
	.id = SM8450_MASTER_ANOC_PCIE_GEM_NOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 2,
	.links = { SM8450_SLAVE_GEM_NOC_CNOC, SM8450_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_snoc_gc = {
	.name = "qnm_snoc_gc",
	.id = SM8450_MASTER_SNOC_GC_MEM_NOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_snoc_sf = {
	.name = "qnm_snoc_sf",
	.id = SM8450_MASTER_SNOC_SF_MEM_NOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { SM8450_SLAVE_GEM_NOC_CNOC, SM8450_SLAVE_LLCC,
		   SM8450_SLAVE_MEM_NOC_PCIE_SNOC },
};

static struct qcom_icc_node qhm_config_noc = {
	.name = "qhm_config_noc",
	.id = SM8450_MASTER_CNOC_LPASS_AG_NOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 6,
	.links = { SM8450_SLAVE_LPASS_CORE_CFG, SM8450_SLAVE_LPASS_LPI_CFG,
		   SM8450_SLAVE_LPASS_MPU_CFG, SM8450_SLAVE_LPASS_TOP_CFG,
		   SM8450_SLAVE_SERVICES_LPASS_AML_NOC, SM8450_SLAVE_SERVICE_LPASS_AG_NOC },
};

static struct qcom_icc_node qxm_lpass_dsp = {
	.name = "qxm_lpass_dsp",
	.id = SM8450_MASTER_LPASS_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 4,
	.links = { SM8450_SLAVE_LPASS_TOP_CFG, SM8450_SLAVE_LPASS_SNOC,
		   SM8450_SLAVE_SERVICES_LPASS_AML_NOC, SM8450_SLAVE_SERVICE_LPASS_AG_NOC },
};

static struct qcom_icc_node llcc_mc = {
	.name = "llcc_mc",
	.id = SM8450_MASTER_LLCC,
	.channels = 4,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_EBI1 },
};

static struct qcom_icc_node qnm_camnoc_hf = {
	.name = "qnm_camnoc_hf",
	.id = SM8450_MASTER_CAMNOC_HF,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qnm_camnoc_icp = {
	.name = "qnm_camnoc_icp",
	.id = SM8450_MASTER_CAMNOC_ICP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qnm_camnoc_sf = {
	.name = "qnm_camnoc_sf",
	.id = SM8450_MASTER_CAMNOC_SF,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qnm_mdp = {
	.name = "qnm_mdp",
	.id = SM8450_MASTER_MDP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qnm_mnoc_cfg = {
	.name = "qnm_mnoc_cfg",
	.id = SM8450_MASTER_CNOC_MNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_SERVICE_MNOC },
};

static struct qcom_icc_node qnm_rot = {
	.name = "qnm_rot",
	.id = SM8450_MASTER_ROTATOR,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qnm_vapss_hcp = {
	.name = "qnm_vapss_hcp",
	.id = SM8450_MASTER_CDSP_HCP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qnm_video = {
	.name = "qnm_video",
	.id = SM8450_MASTER_VIDEO,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qnm_video_cv_cpu = {
	.name = "qnm_video_cv_cpu",
	.id = SM8450_MASTER_VIDEO_CV_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qnm_video_cvp = {
	.name = "qnm_video_cvp",
	.id = SM8450_MASTER_VIDEO_PROC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qnm_video_v_cpu = {
	.name = "qnm_video_v_cpu",
	.id = SM8450_MASTER_VIDEO_V_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qhm_nsp_noc_config = {
	.name = "qhm_nsp_noc_config",
	.id = SM8450_MASTER_CDSP_NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_SERVICE_NSP_NOC },
};

static struct qcom_icc_node qxm_nsp = {
	.name = "qxm_nsp",
	.id = SM8450_MASTER_CDSP_PROC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_CDSP_MEM_NOC },
};

static struct qcom_icc_node qnm_pcie_anoc_cfg = {
	.name = "qnm_pcie_anoc_cfg",
	.id = SM8450_MASTER_PCIE_ANOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_SERVICE_PCIE_ANOC },
};

static struct qcom_icc_node xm_pcie3_0 = {
	.name = "xm_pcie3_0",
	.id = SM8450_MASTER_PCIE_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_ANOC_PCIE_GEM_NOC },
};

static struct qcom_icc_node xm_pcie3_1 = {
	.name = "xm_pcie3_1",
	.id = SM8450_MASTER_PCIE_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_ANOC_PCIE_GEM_NOC },
};

static struct qcom_icc_node qhm_gic = {
	.name = "qhm_gic",
	.id = SM8450_MASTER_GIC_AHB,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node qnm_aggre1_noc = {
	.name = "qnm_aggre1_noc",
	.id = SM8450_MASTER_A1NOC_SNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node qnm_aggre2_noc = {
	.name = "qnm_aggre2_noc",
	.id = SM8450_MASTER_A2NOC_SNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node qnm_lpass_noc = {
	.name = "qnm_lpass_noc",
	.id = SM8450_MASTER_LPASS_ANOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node qnm_snoc_cfg = {
	.name = "qnm_snoc_cfg",
	.id = SM8450_MASTER_SNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_SERVICE_SNOC },
};

static struct qcom_icc_node qxm_pimem = {
	.name = "qxm_pimem",
	.id = SM8450_MASTER_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_SNOC_GEM_NOC_GC },
};

static struct qcom_icc_node xm_gic = {
	.name = "xm_gic",
	.id = SM8450_MASTER_GIC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_SLAVE_SNOC_GEM_NOC_GC },
};

static struct qcom_icc_node qnm_mnoc_hf_disp = {
	.name = "qnm_mnoc_hf_disp",
	.id = SM8450_MASTER_MNOC_HF_MEM_NOC_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_LLCC_DISP },
};

static struct qcom_icc_node qnm_mnoc_sf_disp = {
	.name = "qnm_mnoc_sf_disp",
	.id = SM8450_MASTER_MNOC_SF_MEM_NOC_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_LLCC_DISP },
};

static struct qcom_icc_node qnm_pcie_disp = {
	.name = "qnm_pcie_disp",
	.id = SM8450_MASTER_ANOC_PCIE_GEM_NOC_DISP,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_SLAVE_LLCC_DISP },
};

static struct qcom_icc_node llcc_mc_disp = {
	.name = "llcc_mc_disp",
	.id = SM8450_MASTER_LLCC_DISP,
	.channels = 4,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_SLAVE_EBI1_DISP },
};

static struct qcom_icc_node qnm_mdp_disp = {
	.name = "qnm_mdp_disp",
	.id = SM8450_MASTER_MDP_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_MNOC_HF_MEM_NOC_DISP },
};

static struct qcom_icc_node qnm_rot_disp = {
	.name = "qnm_rot_disp",
	.id = SM8450_MASTER_ROTATOR_DISP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_SLAVE_MNOC_SF_MEM_NOC_DISP },
};

static struct qcom_icc_node qns_a1noc_snoc = {
	.name = "qns_a1noc_snoc",
	.id = SM8450_SLAVE_A1NOC_SNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_MASTER_A1NOC_SNOC },
};

static struct qcom_icc_node srvc_aggre1_noc = {
	.name = "srvc_aggre1_noc",
	.id = SM8450_SLAVE_SERVICE_A1NOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qns_a2noc_snoc = {
	.name = "qns_a2noc_snoc",
	.id = SM8450_SLAVE_A2NOC_SNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_MASTER_A2NOC_SNOC },
};

static struct qcom_icc_node srvc_aggre2_noc = {
	.name = "srvc_aggre2_noc",
	.id = SM8450_SLAVE_SERVICE_A2NOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qup0_core_slave = {
	.name = "qup0_core_slave",
	.id = SM8450_SLAVE_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qup1_core_slave = {
	.name = "qup1_core_slave",
	.id = SM8450_SLAVE_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qup2_core_slave = {
	.name = "qup2_core_slave",
	.id = SM8450_SLAVE_QUP_CORE_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ahb2phy0 = {
	.name = "qhs_ahb2phy0",
	.id = SM8450_SLAVE_AHB2PHY_SOUTH,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ahb2phy1 = {
	.name = "qhs_ahb2phy1",
	.id = SM8450_SLAVE_AHB2PHY_NORTH,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_aoss = {
	.name = "qhs_aoss",
	.id = SM8450_SLAVE_AOSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_camera_cfg = {
	.name = "qhs_camera_cfg",
	.id = SM8450_SLAVE_CAMERA_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SM8450_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_compute_cfg = {
	.name = "qhs_compute_cfg",
	.id = SM8450_SLAVE_CDSP_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { MASTER_CDSP_NOC_CFG },
};

static struct qcom_icc_node qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = SM8450_SLAVE_RBCPR_CX_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_cpr_mmcx = {
	.name = "qhs_cpr_mmcx",
	.id = SM8450_SLAVE_RBCPR_MMCX_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_cpr_mxa = {
	.name = "qhs_cpr_mxa",
	.id = SM8450_SLAVE_RBCPR_MXA_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_cpr_mxc = {
	.name = "qhs_cpr_mxc",
	.id = SM8450_SLAVE_RBCPR_MXC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = SM8450_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_cx_rdpm = {
	.name = "qhs_cx_rdpm",
	.id = SM8450_SLAVE_CX_RDPM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_display_cfg = {
	.name = "qhs_display_cfg",
	.id = SM8450_SLAVE_DISPLAY_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.id = SM8450_SLAVE_GFX3D_CFG,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SM8450_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ipa = {
	.name = "qhs_ipa",
	.id = SM8450_SLAVE_IPA_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ipc_router = {
	.name = "qhs_ipc_router",
	.id = SM8450_SLAVE_IPC_ROUTER_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_lpass_cfg = {
	.name = "qhs_lpass_cfg",
	.id = SM8450_SLAVE_LPASS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { MASTER_CNOC_LPASS_AG_NOC },
};

static struct qcom_icc_node qhs_mss_cfg = {
	.name = "qhs_mss_cfg",
	.id = SM8450_SLAVE_CNOC_MSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_mx_rdpm = {
	.name = "qhs_mx_rdpm",
	.id = SM8450_SLAVE_MX_RDPM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pcie0_cfg = {
	.name = "qhs_pcie0_cfg",
	.id = SM8450_SLAVE_PCIE_0_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pcie1_cfg = {
	.name = "qhs_pcie1_cfg",
	.id = SM8450_SLAVE_PCIE_1_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pdm = {
	.name = "qhs_pdm",
	.id = SM8450_SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = SM8450_SLAVE_PIMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_prng = {
	.name = "qhs_prng",
	.id = SM8450_SLAVE_PRNG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SM8450_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qspi = {
	.name = "qhs_qspi",
	.id = SM8450_SLAVE_QSPI_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qup0 = {
	.name = "qhs_qup0",
	.id = SM8450_SLAVE_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qup1 = {
	.name = "qhs_qup1",
	.id = SM8450_SLAVE_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qup2 = {
	.name = "qhs_qup2",
	.id = SM8450_SLAVE_QUP_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = SM8450_SLAVE_SDCC_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_sdc4 = {
	.name = "qhs_sdc4",
	.id = SM8450_SLAVE_SDCC_4,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_spss_cfg = {
	.name = "qhs_spss_cfg",
	.id = SM8450_SLAVE_SPSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SM8450_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_tlmm = {
	.name = "qhs_tlmm",
	.id = SM8450_SLAVE_TLMM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_tme_cfg = {
	.name = "qhs_tme_cfg",
	.id = SM8450_SLAVE_TME_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.id = SM8450_SLAVE_UFS_MEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_usb3_0 = {
	.name = "qhs_usb3_0",
	.id = SM8450_SLAVE_USB3_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = SM8450_SLAVE_VENUS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.id = SM8450_SLAVE_VSENSE_CTRL_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qns_a1_noc_cfg = {
	.name = "qns_a1_noc_cfg",
	.id = SM8450_SLAVE_A1NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_MASTER_A1NOC_CFG },
};

static struct qcom_icc_node qns_a2_noc_cfg = {
	.name = "qns_a2_noc_cfg",
	.id = SM8450_SLAVE_A2NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_MASTER_A2NOC_CFG },
};

static struct qcom_icc_node qns_ddrss_cfg = {
	.name = "qns_ddrss_cfg",
	.id = SM8450_SLAVE_DDRSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	//FIXME where is link
};

static struct qcom_icc_node qns_mnoc_cfg = {
	.name = "qns_mnoc_cfg",
	.id = SM8450_SLAVE_CNOC_MNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_MASTER_CNOC_MNOC_CFG },
};

static struct qcom_icc_node qns_pcie_anoc_cfg = {
	.name = "qns_pcie_anoc_cfg",
	.id = SM8450_SLAVE_PCIE_ANOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_MASTER_PCIE_ANOC_CFG },
};

static struct qcom_icc_node qns_snoc_cfg = {
	.name = "qns_snoc_cfg",
	.id = SM8450_SLAVE_SNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8450_MASTER_SNOC_CFG },
};

static struct qcom_icc_node qxs_imem = {
	.name = "qxs_imem",
	.id = SM8450_SLAVE_IMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node qxs_pimem = {
	.name = "qxs_pimem",
	.id = SM8450_SLAVE_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node srvc_cnoc = {
	.name = "srvc_cnoc",
	.id = SM8450_SLAVE_SERVICE_CNOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node xs_pcie_0 = {
	.name = "xs_pcie_0",
	.id = SM8450_SLAVE_PCIE_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node xs_pcie_1 = {
	.name = "xs_pcie_1",
	.id = SM8450_SLAVE_PCIE_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SM8450_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SM8450_SLAVE_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node qns_gem_noc_cnoc = {
	.name = "qns_gem_noc_cnoc",
	.id = SM8450_SLAVE_GEM_NOC_CNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_MASTER_GEM_NOC_CNOC },
};

static struct qcom_icc_node qns_llcc = {
	.name = "qns_llcc",
	.id = SM8450_SLAVE_LLCC,
	.channels = 4,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_MASTER_LLCC },
};

static struct qcom_icc_node qns_pcie = {
	.name = "qns_pcie",
	.id = SM8450_SLAVE_MEM_NOC_PCIE_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_MASTER_GEM_NOC_PCIE_SNOC },
};

static struct qcom_icc_node qhs_lpass_core = {
	.name = "qhs_lpass_core",
	.id = SM8450_SLAVE_LPASS_CORE_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_lpass_lpi = {
	.name = "qhs_lpass_lpi",
	.id = SM8450_SLAVE_LPASS_LPI_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_lpass_mpu = {
	.name = "qhs_lpass_mpu",
	.id = SM8450_SLAVE_LPASS_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_lpass_top = {
	.name = "qhs_lpass_top",
	.id = SM8450_SLAVE_LPASS_TOP_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qns_sysnoc = {
	.name = "qns_sysnoc",
	.id = SM8450_SLAVE_LPASS_SNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_MASTER_LPASS_ANOC },
};

static struct qcom_icc_node srvc_niu_aml_noc = {
	.name = "srvc_niu_aml_noc",
	.id = SM8450_SLAVE_SERVICES_LPASS_AML_NOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node srvc_niu_lpass_agnoc = {
	.name = "srvc_niu_lpass_agnoc",
	.id = SM8450_SLAVE_SERVICE_LPASS_AG_NOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.id = SM8450_SLAVE_EBI1,
	.channels = 4,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qns_mem_noc_hf = {
	.name = "qns_mem_noc_hf",
	.id = SM8450_SLAVE_MNOC_HF_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_MASTER_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qns_mem_noc_sf = {
	.name = "qns_mem_noc_sf",
	.id = SM8450_SLAVE_MNOC_SF_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_MASTER_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node srvc_mnoc = {
	.name = "srvc_mnoc",
	.id = SM8450_SLAVE_SERVICE_MNOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qns_nsp_gemnoc = {
	.name = "qns_nsp_gemnoc",
	.id = SM8450_SLAVE_CDSP_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_MASTER_COMPUTE_NOC },
};

static struct qcom_icc_node service_nsp_noc = {
	.name = "service_nsp_noc",
	.id = SM8450_SLAVE_SERVICE_NSP_NOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qns_pcie_mem_noc = {
	.name = "qns_pcie_mem_noc",
	.id = SM8450_SLAVE_ANOC_PCIE_GEM_NOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_MASTER_ANOC_PCIE_GEM_NOC },
};

static struct qcom_icc_node srvc_pcie_aggre_noc = {
	.name = "srvc_pcie_aggre_noc",
	.id = SM8450_SLAVE_SERVICE_PCIE_ANOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qns_gemnoc_gc = {
	.name = "qns_gemnoc_gc",
	.id = SM8450_SLAVE_SNOC_GEM_NOC_GC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8450_MASTER_SNOC_GC_MEM_NOC },
};

static struct qcom_icc_node qns_gemnoc_sf = {
	.name = "qns_gemnoc_sf",
	.id = SM8450_SLAVE_SNOC_GEM_NOC_SF,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_MASTER_SNOC_SF_MEM_NOC },
};

static struct qcom_icc_node srvc_snoc = {
	.name = "srvc_snoc",
	.id = SM8450_SLAVE_SERVICE_SNOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qns_llcc_disp = {
	.name = "qns_llcc_disp",
	.id = SM8450_SLAVE_LLCC_DISP,
	.channels = 4,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8450_MASTER_LLCC_DISP },
};

static struct qcom_icc_node ebi_disp = {
	.name = "ebi_disp",
	.id = SM8450_SLAVE_EBI1_DISP,
	.channels = 4,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qns_mem_noc_hf_disp = {
	.name = "qns_mem_noc_hf_disp",
	.id = SM8450_SLAVE_MNOC_HF_MEM_NOC_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_MASTER_MNOC_HF_MEM_NOC_DISP },
};

static struct qcom_icc_node qns_mem_noc_sf_disp = {
	.name = "qns_mem_noc_sf_disp",
	.id = SM8450_SLAVE_MNOC_SF_MEM_NOC_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8450_MASTER_MNOC_SF_MEM_NOC_DISP },
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
	.num_nodes = 55,
	.nodes = { &qnm_gemnoc_cnoc, &qnm_gemnoc_pcie,
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
		   &qns_a1_noc_cfg, &qns_a2_noc_cfg,
		   &qns_ddrss_cfg, &qns_mnoc_cfg,
		   &qns_pcie_anoc_cfg, &qns_snoc_cfg,
		   &qxs_imem, &qxs_pimem,
		   &srvc_cnoc, &xs_pcie_0,
		   &xs_pcie_1, &xs_qdss_stm,
		   &xs_sys_tcu_cfg },
};

static struct qcom_icc_bcm bcm_co0 = {
	.name = "CO0",
	.num_nodes = 2,
	.nodes = { &qxm_nsp, &qns_nsp_gemnoc },
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
	.num_nodes = 12,
	.nodes = { &qnm_camnoc_hf, &qnm_camnoc_icp,
		   &qnm_camnoc_sf, &qnm_mdp,
		   &qnm_mnoc_cfg, &qnm_rot,
		   &qnm_vapss_hcp, &qnm_video,
		   &qnm_video_cv_cpu, &qnm_video_cvp,
		   &qnm_video_v_cpu, &qns_mem_noc_sf },
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.keepalive = true,
	.vote_scale = 1,
	.num_nodes = 1,
	.nodes = { &qup0_core_slave },
};

static struct qcom_icc_bcm bcm_qup1 = {
	.name = "QUP1",
	.keepalive = true,
	.vote_scale = 1,
	.num_nodes = 1,
	.nodes = { &qup1_core_slave },
};

static struct qcom_icc_bcm bcm_qup2 = {
	.name = "QUP2",
	.keepalive = true,
	.vote_scale = 1,
	.num_nodes = 1,
	.nodes = { &qup2_core_slave },
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_sh1 = {
	.name = "SH1",
	.num_nodes = 7,
	.nodes = { &alm_gpu_tcu, &alm_sys_tcu,
		   &qnm_nsp_gemnoc, &qnm_pcie,
		   &qnm_snoc_gc, &qns_gem_noc_cnoc,
		   &qns_pcie },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.num_nodes = 4,
	.nodes = { &qhm_gic, &qxm_pimem,
		   &xm_gic, &qns_gemnoc_gc },
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.num_nodes = 1,
	.nodes = { &qnm_aggre1_noc },
};

static struct qcom_icc_bcm bcm_sn3 = {
	.name = "SN3",
	.num_nodes = 1,
	.nodes = { &qnm_aggre2_noc },
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.num_nodes = 1,
	.nodes = { &qnm_lpass_noc },
};

static struct qcom_icc_bcm bcm_sn7 = {
	.name = "SN7",
	.num_nodes = 1,
	.nodes = { &qns_pcie_mem_noc },
};

static struct qcom_icc_bcm bcm_acv_disp = {
	.name = "ACV",
	.num_nodes = 1,
	.nodes = { &ebi_disp },
};

static struct qcom_icc_bcm bcm_mc0_disp = {
	.name = "MC0",
	.num_nodes = 1,
	.nodes = { &ebi_disp },
};

static struct qcom_icc_bcm bcm_mm0_disp = {
	.name = "MM0",
	.num_nodes = 1,
	.nodes = { &qns_mem_noc_hf_disp },
};

static struct qcom_icc_bcm bcm_mm1_disp = {
	.name = "MM1",
	.num_nodes = 3,
	.nodes = { &qnm_mdp_disp, &qnm_rot_disp,
		   &qns_mem_noc_sf_disp },
};

static struct qcom_icc_bcm bcm_sh0_disp = {
	.name = "SH0",
	.num_nodes = 1,
	.nodes = { &qns_llcc_disp },
};

static struct qcom_icc_bcm bcm_sh1_disp = {
	.name = "SH1",
	.num_nodes = 1,
	.nodes = { &qnm_pcie_disp },
};

static struct qcom_icc_bcm * const aggre1_noc_bcms[] = {
};

static struct qcom_icc_node * const aggre1_noc_nodes[] = {
	[MASTER_QSPI_0] = &qhm_qspi,
	[MASTER_QUP_1] = &qhm_qup1,
	[MASTER_A1NOC_CFG] = &qnm_a1noc_cfg,
	[MASTER_SDCC_4] = &xm_sdc4,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_USB3_0] = &xm_usb3_0,
	[SLAVE_A1NOC_SNOC] = &qns_a1noc_snoc,
	[SLAVE_SERVICE_A1NOC] = &srvc_aggre1_noc,
};

static const struct qcom_icc_desc sm8450_aggre1_noc = {
	.nodes = aggre1_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre1_noc_nodes),
	.bcms = aggre1_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_noc_bcms),
};

static struct qcom_icc_bcm * const aggre2_noc_bcms[] = {
	&bcm_ce0,
};

static struct qcom_icc_node * const aggre2_noc_nodes[] = {
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_QUP_2] = &qhm_qup2,
	[MASTER_A2NOC_CFG] = &qnm_a2noc_cfg,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_SENSORS_PROC] = &qxm_sensorss_q6,
	[MASTER_SP] = &qxm_sp,
	[MASTER_QDSS_ETR] = &xm_qdss_etr_0,
	[MASTER_QDSS_ETR_1] = &xm_qdss_etr_1,
	[MASTER_SDCC_2] = &xm_sdc2,
	[SLAVE_A2NOC_SNOC] = &qns_a2noc_snoc,
	[SLAVE_SERVICE_A2NOC] = &srvc_aggre2_noc,
};

static const struct qcom_icc_desc sm8450_aggre2_noc = {
	.nodes = aggre2_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre2_noc_nodes),
	.bcms = aggre2_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_noc_bcms),
};

static struct qcom_icc_bcm * const clk_virt_bcms[] = {
	&bcm_qup0,
	&bcm_qup1,
	&bcm_qup2,
};

static struct qcom_icc_node * const clk_virt_nodes[] = {
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[MASTER_QUP_CORE_1] = &qup1_core_master,
	[MASTER_QUP_CORE_2] = &qup2_core_master,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[SLAVE_QUP_CORE_1] = &qup1_core_slave,
	[SLAVE_QUP_CORE_2] = &qup2_core_slave,
};

static const struct qcom_icc_desc sm8450_clk_virt = {
	.nodes = clk_virt_nodes,
	.num_nodes = ARRAY_SIZE(clk_virt_nodes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
};

static struct qcom_icc_bcm * const config_noc_bcms[] = {
	&bcm_cn0,
};

static struct qcom_icc_node * const config_noc_nodes[] = {
	[MASTER_GEM_NOC_CNOC] = &qnm_gemnoc_cnoc,
	[MASTER_GEM_NOC_PCIE_SNOC] = &qnm_gemnoc_pcie,
	[SLAVE_AHB2PHY_SOUTH] = &qhs_ahb2phy0,
	[SLAVE_AHB2PHY_NORTH] = &qhs_ahb2phy1,
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
	[SLAVE_CNOC_MSS] = &qhs_mss_cfg,
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
	[SLAVE_A1NOC_CFG] = &qns_a1_noc_cfg,
	[SLAVE_A2NOC_CFG] = &qns_a2_noc_cfg,
	[SLAVE_DDRSS_CFG] = &qns_ddrss_cfg,
	[SLAVE_CNOC_MNOC_CFG] = &qns_mnoc_cfg,
	[SLAVE_PCIE_ANOC_CFG] = &qns_pcie_anoc_cfg,
	[SLAVE_SNOC_CFG] = &qns_snoc_cfg,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SERVICE_CNOC] = &srvc_cnoc,
	[SLAVE_PCIE_0] = &xs_pcie_0,
	[SLAVE_PCIE_1] = &xs_pcie_1,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sm8450_config_noc = {
	.nodes = config_noc_nodes,
	.num_nodes = ARRAY_SIZE(config_noc_nodes),
	.bcms = config_noc_bcms,
	.num_bcms = ARRAY_SIZE(config_noc_bcms),
};

static struct qcom_icc_bcm * const gem_noc_bcms[] = {
	&bcm_sh0,
	&bcm_sh1,
	&bcm_sh0_disp,
	&bcm_sh1_disp,
};

static struct qcom_icc_node * const gem_noc_nodes[] = {
	[MASTER_GPU_TCU] = &alm_gpu_tcu,
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_APPSS_PROC] = &chm_apps,
	[MASTER_GFX3D] = &qnm_gpu,
	[MASTER_MSS_PROC] = &qnm_mdsp,
	[MASTER_MNOC_HF_MEM_NOC] = &qnm_mnoc_hf,
	[MASTER_MNOC_SF_MEM_NOC] = &qnm_mnoc_sf,
	[MASTER_COMPUTE_NOC] = &qnm_nsp_gemnoc,
	[MASTER_ANOC_PCIE_GEM_NOC] = &qnm_pcie,
	[MASTER_SNOC_GC_MEM_NOC] = &qnm_snoc_gc,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[SLAVE_GEM_NOC_CNOC] = &qns_gem_noc_cnoc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_MEM_NOC_PCIE_SNOC] = &qns_pcie,
	[MASTER_MNOC_HF_MEM_NOC_DISP] = &qnm_mnoc_hf_disp,
	[MASTER_MNOC_SF_MEM_NOC_DISP] = &qnm_mnoc_sf_disp,
	[MASTER_ANOC_PCIE_GEM_NOC_DISP] = &qnm_pcie_disp,
	[SLAVE_LLCC_DISP] = &qns_llcc_disp,
};

static const struct qcom_icc_desc sm8450_gem_noc = {
	.nodes = gem_noc_nodes,
	.num_nodes = ARRAY_SIZE(gem_noc_nodes),
	.bcms = gem_noc_bcms,
	.num_bcms = ARRAY_SIZE(gem_noc_bcms),
};

static struct qcom_icc_bcm * const lpass_ag_noc_bcms[] = {
};

static struct qcom_icc_node * const lpass_ag_noc_nodes[] = {
	[MASTER_CNOC_LPASS_AG_NOC] = &qhm_config_noc,
	[MASTER_LPASS_PROC] = &qxm_lpass_dsp,
	[SLAVE_LPASS_CORE_CFG] = &qhs_lpass_core,
	[SLAVE_LPASS_LPI_CFG] = &qhs_lpass_lpi,
	[SLAVE_LPASS_MPU_CFG] = &qhs_lpass_mpu,
	[SLAVE_LPASS_TOP_CFG] = &qhs_lpass_top,
	[SLAVE_LPASS_SNOC] = &qns_sysnoc,
	[SLAVE_SERVICES_LPASS_AML_NOC] = &srvc_niu_aml_noc,
	[SLAVE_SERVICE_LPASS_AG_NOC] = &srvc_niu_lpass_agnoc,
};

static const struct qcom_icc_desc sm8450_lpass_ag_noc = {
	.nodes = lpass_ag_noc_nodes,
	.num_nodes = ARRAY_SIZE(lpass_ag_noc_nodes),
	.bcms = lpass_ag_noc_bcms,
	.num_bcms = ARRAY_SIZE(lpass_ag_noc_bcms),
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
	&bcm_acv_disp,
	&bcm_mc0_disp,
};

static struct qcom_icc_node * const mc_virt_nodes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
	[MASTER_LLCC_DISP] = &llcc_mc_disp,
	[SLAVE_EBI1_DISP] = &ebi_disp,
};

static const struct qcom_icc_desc sm8450_mc_virt = {
	.nodes = mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm * const mmss_noc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
	&bcm_mm0_disp,
	&bcm_mm1_disp,
};

static struct qcom_icc_node * const mmss_noc_nodes[] = {
	[MASTER_CAMNOC_HF] = &qnm_camnoc_hf,
	[MASTER_CAMNOC_ICP] = &qnm_camnoc_icp,
	[MASTER_CAMNOC_SF] = &qnm_camnoc_sf,
	[MASTER_MDP] = &qnm_mdp,
	[MASTER_CNOC_MNOC_CFG] = &qnm_mnoc_cfg,
	[MASTER_ROTATOR] = &qnm_rot,
	[MASTER_CDSP_HCP] = &qnm_vapss_hcp,
	[MASTER_VIDEO] = &qnm_video,
	[MASTER_VIDEO_CV_PROC] = &qnm_video_cv_cpu,
	[MASTER_VIDEO_PROC] = &qnm_video_cvp,
	[MASTER_VIDEO_V_PROC] = &qnm_video_v_cpu,
	[SLAVE_MNOC_HF_MEM_NOC] = &qns_mem_noc_hf,
	[SLAVE_MNOC_SF_MEM_NOC] = &qns_mem_noc_sf,
	[SLAVE_SERVICE_MNOC] = &srvc_mnoc,
	[MASTER_MDP_DISP] = &qnm_mdp_disp,
	[MASTER_ROTATOR_DISP] = &qnm_rot_disp,
	[SLAVE_MNOC_HF_MEM_NOC_DISP] = &qns_mem_noc_hf_disp,
	[SLAVE_MNOC_SF_MEM_NOC_DISP] = &qns_mem_noc_sf_disp,
};

static const struct qcom_icc_desc sm8450_mmss_noc = {
	.nodes = mmss_noc_nodes,
	.num_nodes = ARRAY_SIZE(mmss_noc_nodes),
	.bcms = mmss_noc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_noc_bcms),
};

static struct qcom_icc_bcm * const nsp_noc_bcms[] = {
	&bcm_co0,
};

static struct qcom_icc_node * const nsp_noc_nodes[] = {
	[MASTER_CDSP_NOC_CFG] = &qhm_nsp_noc_config,
	[MASTER_CDSP_PROC] = &qxm_nsp,
	[SLAVE_CDSP_MEM_NOC] = &qns_nsp_gemnoc,
	[SLAVE_SERVICE_NSP_NOC] = &service_nsp_noc,
};

static const struct qcom_icc_desc sm8450_nsp_noc = {
	.nodes = nsp_noc_nodes,
	.num_nodes = ARRAY_SIZE(nsp_noc_nodes),
	.bcms = nsp_noc_bcms,
	.num_bcms = ARRAY_SIZE(nsp_noc_bcms),
};

static struct qcom_icc_bcm * const pcie_anoc_bcms[] = {
	&bcm_sn7,
};

static struct qcom_icc_node * const pcie_anoc_nodes[] = {
	[MASTER_PCIE_ANOC_CFG] = &qnm_pcie_anoc_cfg,
	[MASTER_PCIE_0] = &xm_pcie3_0,
	[MASTER_PCIE_1] = &xm_pcie3_1,
	[SLAVE_ANOC_PCIE_GEM_NOC] = &qns_pcie_mem_noc,
	[SLAVE_SERVICE_PCIE_ANOC] = &srvc_pcie_aggre_noc,
};

static const struct qcom_icc_desc sm8450_pcie_anoc = {
	.nodes = pcie_anoc_nodes,
	.num_nodes = ARRAY_SIZE(pcie_anoc_nodes),
	.bcms = pcie_anoc_bcms,
	.num_bcms = ARRAY_SIZE(pcie_anoc_bcms),
};

static struct qcom_icc_bcm * const system_noc_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn2,
	&bcm_sn3,
	&bcm_sn4,
};

static struct qcom_icc_node * const system_noc_nodes[] = {
	[MASTER_GIC_AHB] = &qhm_gic,
	[MASTER_A1NOC_SNOC] = &qnm_aggre1_noc,
	[MASTER_A2NOC_SNOC] = &qnm_aggre2_noc,
	[MASTER_LPASS_ANOC] = &qnm_lpass_noc,
	[MASTER_SNOC_CFG] = &qnm_snoc_cfg,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_GIC] = &xm_gic,
	[SLAVE_SNOC_GEM_NOC_GC] = &qns_gemnoc_gc,
	[SLAVE_SNOC_GEM_NOC_SF] = &qns_gemnoc_sf,
	[SLAVE_SERVICE_SNOC] = &srvc_snoc,
};

static const struct qcom_icc_desc sm8450_system_noc = {
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,sm8450-aggre1-noc",
	  .data = &sm8450_aggre1_noc},
	{ .compatible = "qcom,sm8450-aggre2-noc",
	  .data = &sm8450_aggre2_noc},
	{ .compatible = "qcom,sm8450-clk-virt",
	  .data = &sm8450_clk_virt},
	{ .compatible = "qcom,sm8450-config-noc",
	  .data = &sm8450_config_noc},
	{ .compatible = "qcom,sm8450-gem-noc",
	  .data = &sm8450_gem_noc},
	{ .compatible = "qcom,sm8450-lpass-ag-noc",
	  .data = &sm8450_lpass_ag_noc},
	{ .compatible = "qcom,sm8450-mc-virt",
	  .data = &sm8450_mc_virt},
	{ .compatible = "qcom,sm8450-mmss-noc",
	  .data = &sm8450_mmss_noc},
	{ .compatible = "qcom,sm8450-nsp-noc",
	  .data = &sm8450_nsp_noc},
	{ .compatible = "qcom,sm8450-pcie-anoc",
	  .data = &sm8450_pcie_anoc},
	{ .compatible = "qcom,sm8450-system-noc",
	  .data = &sm8450_system_noc},
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-sm8450",
		.of_match_table = qnoc_of_match,
	},
};

static int __init qnoc_driver_init(void)
{
	return platform_driver_register(&qnoc_driver);
}
core_initcall(qnoc_driver_init);

MODULE_DESCRIPTION("sm8450 NoC driver");
MODULE_LICENSE("GPL v2");
