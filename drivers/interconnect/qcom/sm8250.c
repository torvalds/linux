// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <dt-bindings/interconnect/qcom,sm8250.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"
#include "sm8250.h"

static struct qcom_icc_analde qhm_a1analc_cfg = {
	.name = "qhm_a1analc_cfg",
	.id = SM8250_MASTER_A1ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8250_SLAVE_SERVICE_A1ANALC },
};

static struct qcom_icc_analde qhm_qspi = {
	.name = "qhm_qspi",
	.id = SM8250_MASTER_QSPI_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8250_A1ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qhm_qup1 = {
	.name = "qhm_qup1",
	.id = SM8250_MASTER_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8250_A1ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qhm_qup2 = {
	.name = "qhm_qup2",
	.id = SM8250_MASTER_QUP_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8250_A1ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qhm_tsif = {
	.name = "qhm_tsif",
	.id = SM8250_MASTER_TSIF,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8250_A1ANALC_SANALC_SLV },
};

static struct qcom_icc_analde xm_pcie3_modem = {
	.name = "xm_pcie3_modem",
	.id = SM8250_MASTER_PCIE_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8250_SLAVE_AANALC_PCIE_GEM_ANALC_1 },
};

static struct qcom_icc_analde xm_sdc4 = {
	.name = "xm_sdc4",
	.id = SM8250_MASTER_SDCC_4,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8250_A1ANALC_SANALC_SLV },
};

static struct qcom_icc_analde xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.id = SM8250_MASTER_UFS_MEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8250_A1ANALC_SANALC_SLV },
};

static struct qcom_icc_analde xm_usb3_0 = {
	.name = "xm_usb3_0",
	.id = SM8250_MASTER_USB3,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8250_A1ANALC_SANALC_SLV },
};

static struct qcom_icc_analde xm_usb3_1 = {
	.name = "xm_usb3_1",
	.id = SM8250_MASTER_USB3_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8250_A1ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qhm_a2analc_cfg = {
	.name = "qhm_a2analc_cfg",
	.id = SM8250_MASTER_A2ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8250_SLAVE_SERVICE_A2ANALC },
};

static struct qcom_icc_analde qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = SM8250_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8250_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qhm_qup0 = {
	.name = "qhm_qup0",
	.id = SM8250_MASTER_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8250_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qnm_canalc = {
	.name = "qnm_canalc",
	.id = SM8250_MASTER_CANALC_A2ANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8250_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qxm_crypto = {
	.name = "qxm_crypto",
	.id = SM8250_MASTER_CRYPTO_CORE_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8250_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qxm_ipa = {
	.name = "qxm_ipa",
	.id = SM8250_MASTER_IPA,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8250_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde xm_pcie3_0 = {
	.name = "xm_pcie3_0",
	.id = SM8250_MASTER_PCIE,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8250_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde xm_pcie3_1 = {
	.name = "xm_pcie3_1",
	.id = SM8250_MASTER_PCIE_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8250_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde xm_qdss_etr = {
	.name = "xm_qdss_etr",
	.id = SM8250_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8250_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde xm_sdc2 = {
	.name = "xm_sdc2",
	.id = SM8250_MASTER_SDCC_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8250_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde xm_ufs_card = {
	.name = "xm_ufs_card",
	.id = SM8250_MASTER_UFS_CARD,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8250_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qnm_npu = {
	.name = "qnm_npu",
	.id = SM8250_MASTER_NPU,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8250_SLAVE_CDSP_MEM_ANALC },
};

static struct qcom_icc_analde qnm_sanalc = {
	.name = "qnm_sanalc",
	.id = SM8250_SANALC_CANALC_MAS,
	.channels = 1,
	.buswidth = 8,
	.num_links = 49,
	.links = { SM8250_SLAVE_CDSP_CFG,
		   SM8250_SLAVE_CAMERA_CFG,
		   SM8250_SLAVE_TLMM_SOUTH,
		   SM8250_SLAVE_TLMM_ANALRTH,
		   SM8250_SLAVE_SDCC_4,
		   SM8250_SLAVE_TLMM_WEST,
		   SM8250_SLAVE_SDCC_2,
		   SM8250_SLAVE_CANALC_MANALC_CFG,
		   SM8250_SLAVE_UFS_MEM_CFG,
		   SM8250_SLAVE_SANALC_CFG,
		   SM8250_SLAVE_PDM,
		   SM8250_SLAVE_CX_RDPM,
		   SM8250_SLAVE_PCIE_1_CFG,
		   SM8250_SLAVE_A2ANALC_CFG,
		   SM8250_SLAVE_QDSS_CFG,
		   SM8250_SLAVE_DISPLAY_CFG,
		   SM8250_SLAVE_PCIE_2_CFG,
		   SM8250_SLAVE_TCSR,
		   SM8250_SLAVE_DCC_CFG,
		   SM8250_SLAVE_CANALC_DDRSS,
		   SM8250_SLAVE_IPC_ROUTER_CFG,
		   SM8250_SLAVE_PCIE_0_CFG,
		   SM8250_SLAVE_RBCPR_MMCX_CFG,
		   SM8250_SLAVE_NPU_CFG,
		   SM8250_SLAVE_AHB2PHY_SOUTH,
		   SM8250_SLAVE_AHB2PHY_ANALRTH,
		   SM8250_SLAVE_GRAPHICS_3D_CFG,
		   SM8250_SLAVE_VENUS_CFG,
		   SM8250_SLAVE_TSIF,
		   SM8250_SLAVE_IPA_CFG,
		   SM8250_SLAVE_IMEM_CFG,
		   SM8250_SLAVE_USB3,
		   SM8250_SLAVE_SERVICE_CANALC,
		   SM8250_SLAVE_UFS_CARD_CFG,
		   SM8250_SLAVE_USB3_1,
		   SM8250_SLAVE_LPASS,
		   SM8250_SLAVE_RBCPR_CX_CFG,
		   SM8250_SLAVE_A1ANALC_CFG,
		   SM8250_SLAVE_AOSS,
		   SM8250_SLAVE_PRNG,
		   SM8250_SLAVE_VSENSE_CTRL_CFG,
		   SM8250_SLAVE_QSPI_0,
		   SM8250_SLAVE_CRYPTO_0_CFG,
		   SM8250_SLAVE_PIMEM_CFG,
		   SM8250_SLAVE_RBCPR_MX_CFG,
		   SM8250_SLAVE_QUP_0,
		   SM8250_SLAVE_QUP_1,
		   SM8250_SLAVE_QUP_2,
		   SM8250_SLAVE_CLK_CTL
	},
};

static struct qcom_icc_analde xm_qdss_dap = {
	.name = "xm_qdss_dap",
	.id = SM8250_MASTER_QDSS_DAP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 50,
	.links = { SM8250_SLAVE_CDSP_CFG,
		   SM8250_SLAVE_CAMERA_CFG,
		   SM8250_SLAVE_TLMM_SOUTH,
		   SM8250_SLAVE_TLMM_ANALRTH,
		   SM8250_SLAVE_SDCC_4,
		   SM8250_SLAVE_TLMM_WEST,
		   SM8250_SLAVE_SDCC_2,
		   SM8250_SLAVE_CANALC_MANALC_CFG,
		   SM8250_SLAVE_UFS_MEM_CFG,
		   SM8250_SLAVE_SANALC_CFG,
		   SM8250_SLAVE_PDM,
		   SM8250_SLAVE_CX_RDPM,
		   SM8250_SLAVE_PCIE_1_CFG,
		   SM8250_SLAVE_A2ANALC_CFG,
		   SM8250_SLAVE_QDSS_CFG,
		   SM8250_SLAVE_DISPLAY_CFG,
		   SM8250_SLAVE_PCIE_2_CFG,
		   SM8250_SLAVE_TCSR,
		   SM8250_SLAVE_DCC_CFG,
		   SM8250_SLAVE_CANALC_DDRSS,
		   SM8250_SLAVE_IPC_ROUTER_CFG,
		   SM8250_SLAVE_CANALC_A2ANALC,
		   SM8250_SLAVE_PCIE_0_CFG,
		   SM8250_SLAVE_RBCPR_MMCX_CFG,
		   SM8250_SLAVE_NPU_CFG,
		   SM8250_SLAVE_AHB2PHY_SOUTH,
		   SM8250_SLAVE_AHB2PHY_ANALRTH,
		   SM8250_SLAVE_GRAPHICS_3D_CFG,
		   SM8250_SLAVE_VENUS_CFG,
		   SM8250_SLAVE_TSIF,
		   SM8250_SLAVE_IPA_CFG,
		   SM8250_SLAVE_IMEM_CFG,
		   SM8250_SLAVE_USB3,
		   SM8250_SLAVE_SERVICE_CANALC,
		   SM8250_SLAVE_UFS_CARD_CFG,
		   SM8250_SLAVE_USB3_1,
		   SM8250_SLAVE_LPASS,
		   SM8250_SLAVE_RBCPR_CX_CFG,
		   SM8250_SLAVE_A1ANALC_CFG,
		   SM8250_SLAVE_AOSS,
		   SM8250_SLAVE_PRNG,
		   SM8250_SLAVE_VSENSE_CTRL_CFG,
		   SM8250_SLAVE_QSPI_0,
		   SM8250_SLAVE_CRYPTO_0_CFG,
		   SM8250_SLAVE_PIMEM_CFG,
		   SM8250_SLAVE_RBCPR_MX_CFG,
		   SM8250_SLAVE_QUP_0,
		   SM8250_SLAVE_QUP_1,
		   SM8250_SLAVE_QUP_2,
		   SM8250_SLAVE_CLK_CTL
	},
};

static struct qcom_icc_analde qhm_canalc_dc_analc = {
	.name = "qhm_canalc_dc_analc",
	.id = SM8250_MASTER_CANALC_DC_ANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.links = { SM8250_SLAVE_GEM_ANALC_CFG,
		   SM8250_SLAVE_LLCC_CFG
	},
};

static struct qcom_icc_analde alm_gpu_tcu = {
	.name = "alm_gpu_tcu",
	.id = SM8250_MASTER_GPU_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM8250_SLAVE_LLCC,
		   SM8250_SLAVE_GEM_ANALC_SANALC
	},
};

static struct qcom_icc_analde alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.id = SM8250_MASTER_SYS_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM8250_SLAVE_LLCC,
		   SM8250_SLAVE_GEM_ANALC_SANALC
	},
};

static struct qcom_icc_analde chm_apps = {
	.name = "chm_apps",
	.id = SM8250_MASTER_AMPSS_M0,
	.channels = 2,
	.buswidth = 32,
	.num_links = 3,
	.links = { SM8250_SLAVE_LLCC,
		   SM8250_SLAVE_GEM_ANALC_SANALC,
		   SM8250_SLAVE_MEM_ANALC_PCIE_SANALC
	},
};

static struct qcom_icc_analde qhm_gemanalc_cfg = {
	.name = "qhm_gemanalc_cfg",
	.id = SM8250_MASTER_GEM_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 3,
	.links = { SM8250_SLAVE_SERVICE_GEM_ANALC_2,
		   SM8250_SLAVE_SERVICE_GEM_ANALC_1,
		   SM8250_SLAVE_SERVICE_GEM_ANALC
	},
};

static struct qcom_icc_analde qnm_cmpanalc = {
	.name = "qnm_cmpanalc",
	.id = SM8250_MASTER_COMPUTE_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM8250_SLAVE_LLCC,
		   SM8250_SLAVE_GEM_ANALC_SANALC
	},
};

static struct qcom_icc_analde qnm_gpu = {
	.name = "qnm_gpu",
	.id = SM8250_MASTER_GRAPHICS_3D,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM8250_SLAVE_LLCC,
		   SM8250_SLAVE_GEM_ANALC_SANALC },
};

static struct qcom_icc_analde qnm_manalc_hf = {
	.name = "qnm_manalc_hf",
	.id = SM8250_MASTER_MANALC_HF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8250_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_manalc_sf = {
	.name = "qnm_manalc_sf",
	.id = SM8250_MASTER_MANALC_SF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM8250_SLAVE_LLCC,
		   SM8250_SLAVE_GEM_ANALC_SANALC
	},
};

static struct qcom_icc_analde qnm_pcie = {
	.name = "qnm_pcie",
	.id = SM8250_MASTER_AANALC_PCIE_GEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 2,
	.links = { SM8250_SLAVE_LLCC,
		   SM8250_SLAVE_GEM_ANALC_SANALC
	},
};

static struct qcom_icc_analde qnm_sanalc_gc = {
	.name = "qnm_sanalc_gc",
	.id = SM8250_MASTER_SANALC_GC_MEM_ANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8250_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_sanalc_sf = {
	.name = "qnm_sanalc_sf",
	.id = SM8250_MASTER_SANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { SM8250_SLAVE_LLCC,
		   SM8250_SLAVE_GEM_ANALC_SANALC,
		   SM8250_SLAVE_MEM_ANALC_PCIE_SANALC
	},
};

static struct qcom_icc_analde llcc_mc = {
	.name = "llcc_mc",
	.id = SM8250_MASTER_LLCC,
	.channels = 4,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8250_SLAVE_EBI_CH0 },
};

static struct qcom_icc_analde qhm_manalc_cfg = {
	.name = "qhm_manalc_cfg",
	.id = SM8250_MASTER_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8250_SLAVE_SERVICE_MANALC },
};

static struct qcom_icc_analde qnm_camanalc_hf = {
	.name = "qnm_camanalc_hf",
	.id = SM8250_MASTER_CAMANALC_HF,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8250_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_camanalc_icp = {
	.name = "qnm_camanalc_icp",
	.id = SM8250_MASTER_CAMANALC_ICP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8250_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_camanalc_sf = {
	.name = "qnm_camanalc_sf",
	.id = SM8250_MASTER_CAMANALC_SF,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8250_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_video0 = {
	.name = "qnm_video0",
	.id = SM8250_MASTER_VIDEO_P0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8250_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_video1 = {
	.name = "qnm_video1",
	.id = SM8250_MASTER_VIDEO_P1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8250_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_video_cvp = {
	.name = "qnm_video_cvp",
	.id = SM8250_MASTER_VIDEO_PROC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8250_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_mdp0 = {
	.name = "qxm_mdp0",
	.id = SM8250_MASTER_MDP_PORT0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8250_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_mdp1 = {
	.name = "qxm_mdp1",
	.id = SM8250_MASTER_MDP_PORT1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8250_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_rot = {
	.name = "qxm_rot",
	.id = SM8250_MASTER_ROTATOR,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8250_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde amm_npu_sys = {
	.name = "amm_npu_sys",
	.id = SM8250_MASTER_NPU_SYS,
	.channels = 4,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8250_SLAVE_NPU_COMPUTE_ANALC },
};

static struct qcom_icc_analde amm_npu_sys_cdp_w = {
	.name = "amm_npu_sys_cdp_w",
	.id = SM8250_MASTER_NPU_CDP,
	.channels = 2,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8250_SLAVE_NPU_COMPUTE_ANALC },
};

static struct qcom_icc_analde qhm_cfg = {
	.name = "qhm_cfg",
	.id = SM8250_MASTER_NPU_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 9,
	.links = { SM8250_SLAVE_SERVICE_NPU_ANALC,
		   SM8250_SLAVE_ISENSE_CFG,
		   SM8250_SLAVE_NPU_LLM_CFG,
		   SM8250_SLAVE_NPU_INT_DMA_BWMON_CFG,
		   SM8250_SLAVE_NPU_CP,
		   SM8250_SLAVE_NPU_TCM,
		   SM8250_SLAVE_NPU_CAL_DP0,
		   SM8250_SLAVE_NPU_CAL_DP1,
		   SM8250_SLAVE_NPU_DPM
	},
};

static struct qcom_icc_analde qhm_sanalc_cfg = {
	.name = "qhm_sanalc_cfg",
	.id = SM8250_MASTER_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8250_SLAVE_SERVICE_SANALC },
};

static struct qcom_icc_analde qnm_aggre1_analc = {
	.name = "qnm_aggre1_analc",
	.id = SM8250_A1ANALC_SANALC_MAS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8250_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qnm_aggre2_analc = {
	.name = "qnm_aggre2_analc",
	.id = SM8250_A2ANALC_SANALC_MAS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8250_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qnm_gemanalc = {
	.name = "qnm_gemanalc",
	.id = SM8250_MASTER_GEM_ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 6,
	.links = { SM8250_SLAVE_PIMEM,
		   SM8250_SLAVE_OCIMEM,
		   SM8250_SLAVE_APPSS,
		   SM8250_SANALC_CANALC_SLV,
		   SM8250_SLAVE_TCU,
		   SM8250_SLAVE_QDSS_STM
	},
};

static struct qcom_icc_analde qnm_gemanalc_pcie = {
	.name = "qnm_gemanalc_pcie",
	.id = SM8250_MASTER_GEM_ANALC_PCIE_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 3,
	.links = { SM8250_SLAVE_PCIE_2,
		   SM8250_SLAVE_PCIE_0,
		   SM8250_SLAVE_PCIE_1
	},
};

static struct qcom_icc_analde qxm_pimem = {
	.name = "qxm_pimem",
	.id = SM8250_MASTER_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8250_SLAVE_SANALC_GEM_ANALC_GC },
};

static struct qcom_icc_analde xm_gic = {
	.name = "xm_gic",
	.id = SM8250_MASTER_GIC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8250_SLAVE_SANALC_GEM_ANALC_GC },
};

static struct qcom_icc_analde qns_a1analc_sanalc = {
	.name = "qns_a1analc_sanalc",
	.id = SM8250_A1ANALC_SANALC_SLV,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8250_A1ANALC_SANALC_MAS },
};

static struct qcom_icc_analde qns_pcie_modem_mem_analc = {
	.name = "qns_pcie_modem_mem_analc",
	.id = SM8250_SLAVE_AANALC_PCIE_GEM_ANALC_1,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8250_MASTER_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde srvc_aggre1_analc = {
	.name = "srvc_aggre1_analc",
	.id = SM8250_SLAVE_SERVICE_A1ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_a2analc_sanalc = {
	.name = "qns_a2analc_sanalc",
	.id = SM8250_A2ANALC_SANALC_SLV,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8250_A2ANALC_SANALC_MAS },
};

static struct qcom_icc_analde qns_pcie_mem_analc = {
	.name = "qns_pcie_mem_analc",
	.id = SM8250_SLAVE_AANALC_PCIE_GEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8250_MASTER_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde srvc_aggre2_analc = {
	.name = "srvc_aggre2_analc",
	.id = SM8250_SLAVE_SERVICE_A2ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_cdsp_mem_analc = {
	.name = "qns_cdsp_mem_analc",
	.id = SM8250_SLAVE_CDSP_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8250_MASTER_COMPUTE_ANALC },
};

static struct qcom_icc_analde qhs_a1_analc_cfg = {
	.name = "qhs_a1_analc_cfg",
	.id = SM8250_SLAVE_A1ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8250_MASTER_A1ANALC_CFG },
};

static struct qcom_icc_analde qhs_a2_analc_cfg = {
	.name = "qhs_a2_analc_cfg",
	.id = SM8250_SLAVE_A2ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8250_MASTER_A2ANALC_CFG },
};

static struct qcom_icc_analde qhs_ahb2phy0 = {
	.name = "qhs_ahb2phy0",
	.id = SM8250_SLAVE_AHB2PHY_SOUTH,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ahb2phy1 = {
	.name = "qhs_ahb2phy1",
	.id = SM8250_SLAVE_AHB2PHY_ANALRTH,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_aoss = {
	.name = "qhs_aoss",
	.id = SM8250_SLAVE_AOSS,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_camera_cfg = {
	.name = "qhs_camera_cfg",
	.id = SM8250_SLAVE_CAMERA_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SM8250_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_compute_dsp = {
	.name = "qhs_compute_dsp",
	.id = SM8250_SLAVE_CDSP_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = SM8250_SLAVE_RBCPR_CX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cpr_mmcx = {
	.name = "qhs_cpr_mmcx",
	.id = SM8250_SLAVE_RBCPR_MMCX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cpr_mx = {
	.name = "qhs_cpr_mx",
	.id = SM8250_SLAVE_RBCPR_MX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = SM8250_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cx_rdpm = {
	.name = "qhs_cx_rdpm",
	.id = SM8250_SLAVE_CX_RDPM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_dcc_cfg = {
	.name = "qhs_dcc_cfg",
	.id = SM8250_SLAVE_DCC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ddrss_cfg = {
	.name = "qhs_ddrss_cfg",
	.id = SM8250_SLAVE_CANALC_DDRSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8250_MASTER_CANALC_DC_ANALC },
};

static struct qcom_icc_analde qhs_display_cfg = {
	.name = "qhs_display_cfg",
	.id = SM8250_SLAVE_DISPLAY_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.id = SM8250_SLAVE_GRAPHICS_3D_CFG,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SM8250_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ipa = {
	.name = "qhs_ipa",
	.id = SM8250_SLAVE_IPA_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ipc_router = {
	.name = "qhs_ipc_router",
	.id = SM8250_SLAVE_IPC_ROUTER_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_lpass_cfg = {
	.name = "qhs_lpass_cfg",
	.id = SM8250_SLAVE_LPASS,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_manalc_cfg = {
	.name = "qhs_manalc_cfg",
	.id = SM8250_SLAVE_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8250_MASTER_CANALC_MANALC_CFG },
};

static struct qcom_icc_analde qhs_npu_cfg = {
	.name = "qhs_npu_cfg",
	.id = SM8250_SLAVE_NPU_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8250_MASTER_NPU_ANALC_CFG },
};

static struct qcom_icc_analde qhs_pcie0_cfg = {
	.name = "qhs_pcie0_cfg",
	.id = SM8250_SLAVE_PCIE_0_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pcie1_cfg = {
	.name = "qhs_pcie1_cfg",
	.id = SM8250_SLAVE_PCIE_1_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pcie_modem_cfg = {
	.name = "qhs_pcie_modem_cfg",
	.id = SM8250_SLAVE_PCIE_2_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pdm = {
	.name = "qhs_pdm",
	.id = SM8250_SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = SM8250_SLAVE_PIMEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_prng = {
	.name = "qhs_prng",
	.id = SM8250_SLAVE_PRNG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SM8250_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qspi = {
	.name = "qhs_qspi",
	.id = SM8250_SLAVE_QSPI_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qup0 = {
	.name = "qhs_qup0",
	.id = SM8250_SLAVE_QUP_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qup1 = {
	.name = "qhs_qup1",
	.id = SM8250_SLAVE_QUP_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qup2 = {
	.name = "qhs_qup2",
	.id = SM8250_SLAVE_QUP_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = SM8250_SLAVE_SDCC_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_sdc4 = {
	.name = "qhs_sdc4",
	.id = SM8250_SLAVE_SDCC_4,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_sanalc_cfg = {
	.name = "qhs_sanalc_cfg",
	.id = SM8250_SLAVE_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8250_MASTER_SANALC_CFG },
};

static struct qcom_icc_analde qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SM8250_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tlmm0 = {
	.name = "qhs_tlmm0",
	.id = SM8250_SLAVE_TLMM_ANALRTH,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tlmm1 = {
	.name = "qhs_tlmm1",
	.id = SM8250_SLAVE_TLMM_SOUTH,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tlmm2 = {
	.name = "qhs_tlmm2",
	.id = SM8250_SLAVE_TLMM_WEST,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tsif = {
	.name = "qhs_tsif",
	.id = SM8250_SLAVE_TSIF,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ufs_card_cfg = {
	.name = "qhs_ufs_card_cfg",
	.id = SM8250_SLAVE_UFS_CARD_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.id = SM8250_SLAVE_UFS_MEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_usb3_0 = {
	.name = "qhs_usb3_0",
	.id = SM8250_SLAVE_USB3,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_usb3_1 = {
	.name = "qhs_usb3_1",
	.id = SM8250_SLAVE_USB3_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = SM8250_SLAVE_VENUS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.id = SM8250_SLAVE_VSENSE_CTRL_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_canalc_a2analc = {
	.name = "qns_canalc_a2analc",
	.id = SM8250_SLAVE_CANALC_A2ANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8250_MASTER_CANALC_A2ANALC },
};

static struct qcom_icc_analde srvc_canalc = {
	.name = "srvc_canalc",
	.id = SM8250_SLAVE_SERVICE_CANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_llcc = {
	.name = "qhs_llcc",
	.id = SM8250_SLAVE_LLCC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_memanalc = {
	.name = "qhs_memanalc",
	.id = SM8250_SLAVE_GEM_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8250_MASTER_GEM_ANALC_CFG },
};

static struct qcom_icc_analde qns_gem_analc_sanalc = {
	.name = "qns_gem_analc_sanalc",
	.id = SM8250_SLAVE_GEM_ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8250_MASTER_GEM_ANALC_SANALC },
};

static struct qcom_icc_analde qns_llcc = {
	.name = "qns_llcc",
	.id = SM8250_SLAVE_LLCC,
	.channels = 4,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8250_MASTER_LLCC },
};

static struct qcom_icc_analde qns_sys_pcie = {
	.name = "qns_sys_pcie",
	.id = SM8250_SLAVE_MEM_ANALC_PCIE_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8250_MASTER_GEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde srvc_even_gemanalc = {
	.name = "srvc_even_gemanalc",
	.id = SM8250_SLAVE_SERVICE_GEM_ANALC_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde srvc_odd_gemanalc = {
	.name = "srvc_odd_gemanalc",
	.id = SM8250_SLAVE_SERVICE_GEM_ANALC_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde srvc_sys_gemanalc = {
	.name = "srvc_sys_gemanalc",
	.id = SM8250_SLAVE_SERVICE_GEM_ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde ebi = {
	.name = "ebi",
	.id = SM8250_SLAVE_EBI_CH0,
	.channels = 4,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_mem_analc_hf = {
	.name = "qns_mem_analc_hf",
	.id = SM8250_SLAVE_MANALC_HF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8250_MASTER_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qns_mem_analc_sf = {
	.name = "qns_mem_analc_sf",
	.id = SM8250_SLAVE_MANALC_SF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8250_MASTER_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde srvc_manalc = {
	.name = "srvc_manalc",
	.id = SM8250_SLAVE_SERVICE_MANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cal_dp0 = {
	.name = "qhs_cal_dp0",
	.id = SM8250_SLAVE_NPU_CAL_DP0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cal_dp1 = {
	.name = "qhs_cal_dp1",
	.id = SM8250_SLAVE_NPU_CAL_DP1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cp = {
	.name = "qhs_cp",
	.id = SM8250_SLAVE_NPU_CP,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_dma_bwmon = {
	.name = "qhs_dma_bwmon",
	.id = SM8250_SLAVE_NPU_INT_DMA_BWMON_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_dpm = {
	.name = "qhs_dpm",
	.id = SM8250_SLAVE_NPU_DPM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_isense = {
	.name = "qhs_isense",
	.id = SM8250_SLAVE_ISENSE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_llm = {
	.name = "qhs_llm",
	.id = SM8250_SLAVE_NPU_LLM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tcm = {
	.name = "qhs_tcm",
	.id = SM8250_SLAVE_NPU_TCM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_npu_sys = {
	.name = "qns_npu_sys",
	.id = SM8250_SLAVE_NPU_COMPUTE_ANALC,
	.channels = 2,
	.buswidth = 32,
};

static struct qcom_icc_analde srvc_analc = {
	.name = "srvc_analc",
	.id = SM8250_SLAVE_SERVICE_NPU_ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_apss = {
	.name = "qhs_apss",
	.id = SM8250_SLAVE_APPSS,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qns_canalc = {
	.name = "qns_canalc",
	.id = SM8250_SANALC_CANALC_SLV,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8250_SANALC_CANALC_MAS },
};

static struct qcom_icc_analde qns_gemanalc_gc = {
	.name = "qns_gemanalc_gc",
	.id = SM8250_SLAVE_SANALC_GEM_ANALC_GC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8250_MASTER_SANALC_GC_MEM_ANALC },
};

static struct qcom_icc_analde qns_gemanalc_sf = {
	.name = "qns_gemanalc_sf",
	.id = SM8250_SLAVE_SANALC_GEM_ANALC_SF,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8250_MASTER_SANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxs_imem = {
	.name = "qxs_imem",
	.id = SM8250_SLAVE_OCIMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qxs_pimem = {
	.name = "qxs_pimem",
	.id = SM8250_SLAVE_PIMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde srvc_sanalc = {
	.name = "srvc_sanalc",
	.id = SM8250_SLAVE_SERVICE_SANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde xs_pcie_0 = {
	.name = "xs_pcie_0",
	.id = SM8250_SLAVE_PCIE_0,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde xs_pcie_1 = {
	.name = "xs_pcie_1",
	.id = SM8250_SLAVE_PCIE_1,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde xs_pcie_modem = {
	.name = "xs_pcie_modem",
	.id = SM8250_SLAVE_PCIE_2,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SM8250_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SM8250_SLAVE_TCU,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qup0_core_master = {
	.name = "qup0_core_master",
	.id = SM8250_MASTER_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8250_SLAVE_QUP_CORE_0 },
};

static struct qcom_icc_analde qup1_core_master = {
	.name = "qup1_core_master",
	.id = SM8250_MASTER_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8250_SLAVE_QUP_CORE_1 },
};

static struct qcom_icc_analde qup2_core_master = {
	.name = "qup2_core_master",
	.id = SM8250_MASTER_QUP_CORE_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8250_SLAVE_QUP_CORE_2 },
};

static struct qcom_icc_analde qup0_core_slave = {
	.name = "qup0_core_slave",
	.id = SM8250_SLAVE_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qup1_core_slave = {
	.name = "qup1_core_slave",
	.id = SM8250_SLAVE_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qup2_core_slave = {
	.name = "qup2_core_slave",
	.id = SM8250_SLAVE_QUP_CORE_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
	.enable_mask = BIT(3),
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &ebi },
};

static struct qcom_icc_bcm bcm_mc0 = {
	.name = "MC0",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &ebi },
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_mm0 = {
	.name = "MM0",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &qns_mem_analc_hf },
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_mm1 = {
	.name = "MM1",
	.keepalive = false,
	.num_analdes = 3,
	.analdes = { &qnm_camanalc_hf, &qxm_mdp0, &qxm_mdp1 },
};

static struct qcom_icc_bcm bcm_sh2 = {
	.name = "SH2",
	.keepalive = false,
	.num_analdes = 2,
	.analdes = { &alm_gpu_tcu, &alm_sys_tcu },
};

static struct qcom_icc_bcm bcm_mm2 = {
	.name = "MM2",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qns_mem_analc_sf },
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.keepalive = false,
	.num_analdes = 3,
	.analdes = { &qup0_core_master, &qup1_core_master, &qup2_core_master },
};

static struct qcom_icc_bcm bcm_sh3 = {
	.name = "SH3",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qnm_cmpanalc },
};

static struct qcom_icc_bcm bcm_mm3 = {
	.name = "MM3",
	.keepalive = false,
	.num_analdes = 5,
	.analdes = { &qnm_camanalc_icp, &qnm_camanalc_sf, &qnm_video0, &qnm_video1, &qnm_video_cvp },
};

static struct qcom_icc_bcm bcm_sh4 = {
	.name = "SH4",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &chm_apps },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &qns_gemanalc_sf },
};

static struct qcom_icc_bcm bcm_co0 = {
	.name = "CO0",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qns_cdsp_mem_analc },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.keepalive = true,
	.num_analdes = 52,
	.analdes = { &qnm_sanalc,
		   &xm_qdss_dap,
		   &qhs_a1_analc_cfg,
		   &qhs_a2_analc_cfg,
		   &qhs_ahb2phy0,
		   &qhs_ahb2phy1,
		   &qhs_aoss,
		   &qhs_camera_cfg,
		   &qhs_clk_ctl,
		   &qhs_compute_dsp,
		   &qhs_cpr_cx,
		   &qhs_cpr_mmcx,
		   &qhs_cpr_mx,
		   &qhs_crypto0_cfg,
		   &qhs_cx_rdpm,
		   &qhs_dcc_cfg,
		   &qhs_ddrss_cfg,
		   &qhs_display_cfg,
		   &qhs_gpuss_cfg,
		   &qhs_imem_cfg,
		   &qhs_ipa,
		   &qhs_ipc_router,
		   &qhs_lpass_cfg,
		   &qhs_manalc_cfg,
		   &qhs_npu_cfg,
		   &qhs_pcie0_cfg,
		   &qhs_pcie1_cfg,
		   &qhs_pcie_modem_cfg,
		   &qhs_pdm,
		   &qhs_pimem_cfg,
		   &qhs_prng,
		   &qhs_qdss_cfg,
		   &qhs_qspi,
		   &qhs_qup0,
		   &qhs_qup1,
		   &qhs_qup2,
		   &qhs_sdc2,
		   &qhs_sdc4,
		   &qhs_sanalc_cfg,
		   &qhs_tcsr,
		   &qhs_tlmm0,
		   &qhs_tlmm1,
		   &qhs_tlmm2,
		   &qhs_tsif,
		   &qhs_ufs_card_cfg,
		   &qhs_ufs_mem_cfg,
		   &qhs_usb3_0,
		   &qhs_usb3_1,
		   &qhs_venus_cfg,
		   &qhs_vsense_ctrl_cfg,
		   &qns_canalc_a2analc,
		   &srvc_canalc
	},
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qxs_imem },
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qns_gemanalc_gc },
};

static struct qcom_icc_bcm bcm_co2 = {
	.name = "CO2",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qnm_npu },
};

static struct qcom_icc_bcm bcm_sn3 = {
	.name = "SN3",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qxs_pimem },
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &xs_qdss_stm },
};

static struct qcom_icc_bcm bcm_sn5 = {
	.name = "SN5",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &xs_pcie_modem },
};

static struct qcom_icc_bcm bcm_sn6 = {
	.name = "SN6",
	.keepalive = false,
	.num_analdes = 2,
	.analdes = { &xs_pcie_0, &xs_pcie_1 },
};

static struct qcom_icc_bcm bcm_sn7 = {
	.name = "SN7",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qnm_aggre1_analc },
};

static struct qcom_icc_bcm bcm_sn8 = {
	.name = "SN8",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qnm_aggre2_analc },
};

static struct qcom_icc_bcm bcm_sn9 = {
	.name = "SN9",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qnm_gemanalc_pcie },
};

static struct qcom_icc_bcm bcm_sn11 = {
	.name = "SN11",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qnm_gemanalc },
};

static struct qcom_icc_bcm bcm_sn12 = {
	.name = "SN12",
	.keepalive = false,
	.num_analdes = 2,
	.analdes = { &qns_pcie_modem_mem_analc, &qns_pcie_mem_analc },
};

static struct qcom_icc_bcm * const aggre1_analc_bcms[] = {
	&bcm_sn12,
};

static struct qcom_icc_analde * const aggre1_analc_analdes[] = {
	[MASTER_A1ANALC_CFG] = &qhm_a1analc_cfg,
	[MASTER_QSPI_0] = &qhm_qspi,
	[MASTER_QUP_1] = &qhm_qup1,
	[MASTER_QUP_2] = &qhm_qup2,
	[MASTER_TSIF] = &qhm_tsif,
	[MASTER_PCIE_2] = &xm_pcie3_modem,
	[MASTER_SDCC_4] = &xm_sdc4,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_USB3] = &xm_usb3_0,
	[MASTER_USB3_1] = &xm_usb3_1,
	[A1ANALC_SANALC_SLV] = &qns_a1analc_sanalc,
	[SLAVE_AANALC_PCIE_GEM_ANALC_1] = &qns_pcie_modem_mem_analc,
	[SLAVE_SERVICE_A1ANALC] = &srvc_aggre1_analc,
};

static const struct qcom_icc_desc sm8250_aggre1_analc = {
	.analdes = aggre1_analc_analdes,
	.num_analdes = ARRAY_SIZE(aggre1_analc_analdes),
	.bcms = aggre1_analc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_analc_bcms),
};

static struct qcom_icc_bcm * const aggre2_analc_bcms[] = {
	&bcm_ce0,
	&bcm_sn12,
};

static struct qcom_icc_bcm * const qup_virt_bcms[] = {
	&bcm_qup0,
};

static struct qcom_icc_analde *qup_virt_analdes[] = {
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[MASTER_QUP_CORE_1] = &qup1_core_master,
	[MASTER_QUP_CORE_2] = &qup2_core_master,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[SLAVE_QUP_CORE_1] = &qup1_core_slave,
	[SLAVE_QUP_CORE_2] = &qup2_core_slave,
};

static const struct qcom_icc_desc sm8250_qup_virt = {
	.analdes = qup_virt_analdes,
	.num_analdes = ARRAY_SIZE(qup_virt_analdes),
	.bcms = qup_virt_bcms,
	.num_bcms = ARRAY_SIZE(qup_virt_bcms),
};

static struct qcom_icc_analde * const aggre2_analc_analdes[] = {
	[MASTER_A2ANALC_CFG] = &qhm_a2analc_cfg,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_CANALC_A2ANALC] = &qnm_canalc,
	[MASTER_CRYPTO_CORE_0] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_PCIE] = &xm_pcie3_0,
	[MASTER_PCIE_1] = &xm_pcie3_1,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_UFS_CARD] = &xm_ufs_card,
	[A2ANALC_SANALC_SLV] = &qns_a2analc_sanalc,
	[SLAVE_AANALC_PCIE_GEM_ANALC] = &qns_pcie_mem_analc,
	[SLAVE_SERVICE_A2ANALC] = &srvc_aggre2_analc,
};

static const struct qcom_icc_desc sm8250_aggre2_analc = {
	.analdes = aggre2_analc_analdes,
	.num_analdes = ARRAY_SIZE(aggre2_analc_analdes),
	.bcms = aggre2_analc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_analc_bcms),
};

static struct qcom_icc_bcm * const compute_analc_bcms[] = {
	&bcm_co0,
	&bcm_co2,
};

static struct qcom_icc_analde * const compute_analc_analdes[] = {
	[MASTER_NPU] = &qnm_npu,
	[SLAVE_CDSP_MEM_ANALC] = &qns_cdsp_mem_analc,
};

static const struct qcom_icc_desc sm8250_compute_analc = {
	.analdes = compute_analc_analdes,
	.num_analdes = ARRAY_SIZE(compute_analc_analdes),
	.bcms = compute_analc_bcms,
	.num_bcms = ARRAY_SIZE(compute_analc_bcms),
};

static struct qcom_icc_bcm * const config_analc_bcms[] = {
	&bcm_cn0,
};

static struct qcom_icc_analde * const config_analc_analdes[] = {
	[SANALC_CANALC_MAS] = &qnm_sanalc,
	[MASTER_QDSS_DAP] = &xm_qdss_dap,
	[SLAVE_A1ANALC_CFG] = &qhs_a1_analc_cfg,
	[SLAVE_A2ANALC_CFG] = &qhs_a2_analc_cfg,
	[SLAVE_AHB2PHY_SOUTH] = &qhs_ahb2phy0,
	[SLAVE_AHB2PHY_ANALRTH] = &qhs_ahb2phy1,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CDSP_CFG] = &qhs_compute_dsp,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MMCX_CFG] = &qhs_cpr_mmcx,
	[SLAVE_RBCPR_MX_CFG] = &qhs_cpr_mx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_CX_RDPM] = &qhs_cx_rdpm,
	[SLAVE_DCC_CFG] = &qhs_dcc_cfg,
	[SLAVE_CANALC_DDRSS] = &qhs_ddrss_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_GRAPHICS_3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_IPC_ROUTER_CFG] = &qhs_ipc_router,
	[SLAVE_LPASS] = &qhs_lpass_cfg,
	[SLAVE_CANALC_MANALC_CFG] = &qhs_manalc_cfg,
	[SLAVE_NPU_CFG] = &qhs_npu_cfg,
	[SLAVE_PCIE_0_CFG] = &qhs_pcie0_cfg,
	[SLAVE_PCIE_1_CFG] = &qhs_pcie1_cfg,
	[SLAVE_PCIE_2_CFG] = &qhs_pcie_modem_cfg,
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
	[SLAVE_SANALC_CFG] = &qhs_sanalc_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM_ANALRTH] = &qhs_tlmm0,
	[SLAVE_TLMM_SOUTH] = &qhs_tlmm1,
	[SLAVE_TLMM_WEST] = &qhs_tlmm2,
	[SLAVE_TSIF] = &qhs_tsif,
	[SLAVE_UFS_CARD_CFG] = &qhs_ufs_card_cfg,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB3] = &qhs_usb3_0,
	[SLAVE_USB3_1] = &qhs_usb3_1,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_CANALC_A2ANALC] = &qns_canalc_a2analc,
	[SLAVE_SERVICE_CANALC] = &srvc_canalc,
};

static const struct qcom_icc_desc sm8250_config_analc = {
	.analdes = config_analc_analdes,
	.num_analdes = ARRAY_SIZE(config_analc_analdes),
	.bcms = config_analc_bcms,
	.num_bcms = ARRAY_SIZE(config_analc_bcms),
};

static struct qcom_icc_bcm * const dc_analc_bcms[] = {
};

static struct qcom_icc_analde * const dc_analc_analdes[] = {
	[MASTER_CANALC_DC_ANALC] = &qhm_canalc_dc_analc,
	[SLAVE_LLCC_CFG] = &qhs_llcc,
	[SLAVE_GEM_ANALC_CFG] = &qhs_memanalc,
};

static const struct qcom_icc_desc sm8250_dc_analc = {
	.analdes = dc_analc_analdes,
	.num_analdes = ARRAY_SIZE(dc_analc_analdes),
	.bcms = dc_analc_bcms,
	.num_bcms = ARRAY_SIZE(dc_analc_bcms),
};

static struct qcom_icc_bcm * const gem_analc_bcms[] = {
	&bcm_sh0,
	&bcm_sh2,
	&bcm_sh3,
	&bcm_sh4,
};

static struct qcom_icc_analde * const gem_analc_analdes[] = {
	[MASTER_GPU_TCU] = &alm_gpu_tcu,
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_AMPSS_M0] = &chm_apps,
	[MASTER_GEM_ANALC_CFG] = &qhm_gemanalc_cfg,
	[MASTER_COMPUTE_ANALC] = &qnm_cmpanalc,
	[MASTER_GRAPHICS_3D] = &qnm_gpu,
	[MASTER_MANALC_HF_MEM_ANALC] = &qnm_manalc_hf,
	[MASTER_MANALC_SF_MEM_ANALC] = &qnm_manalc_sf,
	[MASTER_AANALC_PCIE_GEM_ANALC] = &qnm_pcie,
	[MASTER_SANALC_GC_MEM_ANALC] = &qnm_sanalc_gc,
	[MASTER_SANALC_SF_MEM_ANALC] = &qnm_sanalc_sf,
	[SLAVE_GEM_ANALC_SANALC] = &qns_gem_analc_sanalc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_MEM_ANALC_PCIE_SANALC] = &qns_sys_pcie,
	[SLAVE_SERVICE_GEM_ANALC_1] = &srvc_even_gemanalc,
	[SLAVE_SERVICE_GEM_ANALC_2] = &srvc_odd_gemanalc,
	[SLAVE_SERVICE_GEM_ANALC] = &srvc_sys_gemanalc,
};

static const struct qcom_icc_desc sm8250_gem_analc = {
	.analdes = gem_analc_analdes,
	.num_analdes = ARRAY_SIZE(gem_analc_analdes),
	.bcms = gem_analc_bcms,
	.num_bcms = ARRAY_SIZE(gem_analc_bcms),
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
};

static struct qcom_icc_analde * const mc_virt_analdes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI_CH0] = &ebi,
};

static const struct qcom_icc_desc sm8250_mc_virt = {
	.analdes = mc_virt_analdes,
	.num_analdes = ARRAY_SIZE(mc_virt_analdes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm * const mmss_analc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
	&bcm_mm2,
	&bcm_mm3,
};

static struct qcom_icc_analde * const mmss_analc_analdes[] = {
	[MASTER_CANALC_MANALC_CFG] = &qhm_manalc_cfg,
	[MASTER_CAMANALC_HF] = &qnm_camanalc_hf,
	[MASTER_CAMANALC_ICP] = &qnm_camanalc_icp,
	[MASTER_CAMANALC_SF] = &qnm_camanalc_sf,
	[MASTER_VIDEO_P0] = &qnm_video0,
	[MASTER_VIDEO_P1] = &qnm_video1,
	[MASTER_VIDEO_PROC] = &qnm_video_cvp,
	[MASTER_MDP_PORT0] = &qxm_mdp0,
	[MASTER_MDP_PORT1] = &qxm_mdp1,
	[MASTER_ROTATOR] = &qxm_rot,
	[SLAVE_MANALC_HF_MEM_ANALC] = &qns_mem_analc_hf,
	[SLAVE_MANALC_SF_MEM_ANALC] = &qns_mem_analc_sf,
	[SLAVE_SERVICE_MANALC] = &srvc_manalc,
};

static const struct qcom_icc_desc sm8250_mmss_analc = {
	.analdes = mmss_analc_analdes,
	.num_analdes = ARRAY_SIZE(mmss_analc_analdes),
	.bcms = mmss_analc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_analc_bcms),
};

static struct qcom_icc_bcm * const npu_analc_bcms[] = {
};

static struct qcom_icc_analde * const npu_analc_analdes[] = {
	[MASTER_NPU_SYS] = &amm_npu_sys,
	[MASTER_NPU_CDP] = &amm_npu_sys_cdp_w,
	[MASTER_NPU_ANALC_CFG] = &qhm_cfg,
	[SLAVE_NPU_CAL_DP0] = &qhs_cal_dp0,
	[SLAVE_NPU_CAL_DP1] = &qhs_cal_dp1,
	[SLAVE_NPU_CP] = &qhs_cp,
	[SLAVE_NPU_INT_DMA_BWMON_CFG] = &qhs_dma_bwmon,
	[SLAVE_NPU_DPM] = &qhs_dpm,
	[SLAVE_ISENSE_CFG] = &qhs_isense,
	[SLAVE_NPU_LLM_CFG] = &qhs_llm,
	[SLAVE_NPU_TCM] = &qhs_tcm,
	[SLAVE_NPU_COMPUTE_ANALC] = &qns_npu_sys,
	[SLAVE_SERVICE_NPU_ANALC] = &srvc_analc,
};

static const struct qcom_icc_desc sm8250_npu_analc = {
	.analdes = npu_analc_analdes,
	.num_analdes = ARRAY_SIZE(npu_analc_analdes),
	.bcms = npu_analc_bcms,
	.num_bcms = ARRAY_SIZE(npu_analc_bcms),
};

static struct qcom_icc_bcm * const system_analc_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn11,
	&bcm_sn2,
	&bcm_sn3,
	&bcm_sn4,
	&bcm_sn5,
	&bcm_sn6,
	&bcm_sn7,
	&bcm_sn8,
	&bcm_sn9,
};

static struct qcom_icc_analde * const system_analc_analdes[] = {
	[MASTER_SANALC_CFG] = &qhm_sanalc_cfg,
	[A1ANALC_SANALC_MAS] = &qnm_aggre1_analc,
	[A2ANALC_SANALC_MAS] = &qnm_aggre2_analc,
	[MASTER_GEM_ANALC_SANALC] = &qnm_gemanalc,
	[MASTER_GEM_ANALC_PCIE_SANALC] = &qnm_gemanalc_pcie,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_GIC] = &xm_gic,
	[SLAVE_APPSS] = &qhs_apss,
	[SANALC_CANALC_SLV] = &qns_canalc,
	[SLAVE_SANALC_GEM_ANALC_GC] = &qns_gemanalc_gc,
	[SLAVE_SANALC_GEM_ANALC_SF] = &qns_gemanalc_sf,
	[SLAVE_OCIMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SERVICE_SANALC] = &srvc_sanalc,
	[SLAVE_PCIE_0] = &xs_pcie_0,
	[SLAVE_PCIE_1] = &xs_pcie_1,
	[SLAVE_PCIE_2] = &xs_pcie_modem,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sm8250_system_analc = {
	.analdes = system_analc_analdes,
	.num_analdes = ARRAY_SIZE(system_analc_analdes),
	.bcms = system_analc_bcms,
	.num_bcms = ARRAY_SIZE(system_analc_bcms),
};

static const struct of_device_id qanalc_of_match[] = {
	{ .compatible = "qcom,sm8250-aggre1-analc",
	  .data = &sm8250_aggre1_analc},
	{ .compatible = "qcom,sm8250-aggre2-analc",
	  .data = &sm8250_aggre2_analc},
	{ .compatible = "qcom,sm8250-compute-analc",
	  .data = &sm8250_compute_analc},
	{ .compatible = "qcom,sm8250-config-analc",
	  .data = &sm8250_config_analc},
	{ .compatible = "qcom,sm8250-dc-analc",
	  .data = &sm8250_dc_analc},
	{ .compatible = "qcom,sm8250-gem-analc",
	  .data = &sm8250_gem_analc},
	{ .compatible = "qcom,sm8250-mc-virt",
	  .data = &sm8250_mc_virt},
	{ .compatible = "qcom,sm8250-mmss-analc",
	  .data = &sm8250_mmss_analc},
	{ .compatible = "qcom,sm8250-npu-analc",
	  .data = &sm8250_npu_analc},
	{ .compatible = "qcom,sm8250-qup-virt",
	  .data = &sm8250_qup_virt },
	{ .compatible = "qcom,sm8250-system-analc",
	  .data = &sm8250_system_analc},
	{ }
};
MODULE_DEVICE_TABLE(of, qanalc_of_match);

static struct platform_driver qanalc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove_new = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qanalc-sm8250",
		.of_match_table = qanalc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(qanalc_driver);

MODULE_DESCRIPTION("Qualcomm SM8250 AnalC driver");
MODULE_LICENSE("GPL v2");
