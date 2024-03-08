// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021, Linaro Limited
 *
 */

#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <dt-bindings/interconnect/qcom,sm8350.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"
#include "sm8350.h"

static struct qcom_icc_analde qhm_qspi = {
	.name = "qhm_qspi",
	.id = SM8350_MASTER_QSPI_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qup0 = {
	.name = "qhm_qup0",
	.id = SM8350_MASTER_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qup1 = {
	.name = "qhm_qup1",
	.id = SM8350_MASTER_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qup2 = {
	.name = "qhm_qup2",
	.id = SM8350_MASTER_QUP_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qnm_a1analc_cfg = {
	.name = "qnm_a1analc_cfg",
	.id = SM8350_MASTER_A1ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_SERVICE_A1ANALC },
};

static struct qcom_icc_analde xm_sdc4 = {
	.name = "xm_sdc4",
	.id = SM8350_MASTER_SDCC_4,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.id = SM8350_MASTER_UFS_MEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_usb3_0 = {
	.name = "xm_usb3_0",
	.id = SM8350_MASTER_USB3_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_usb3_1 = {
	.name = "xm_usb3_1",
	.id = SM8350_MASTER_USB3_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = SM8350_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qnm_a2analc_cfg = {
	.name = "qnm_a2analc_cfg",
	.id = SM8350_MASTER_A2ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_SERVICE_A2ANALC },
};

static struct qcom_icc_analde qxm_crypto = {
	.name = "qxm_crypto",
	.id = SM8350_MASTER_CRYPTO,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qxm_ipa = {
	.name = "qxm_ipa",
	.id = SM8350_MASTER_IPA,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde xm_pcie3_0 = {
	.name = "xm_pcie3_0",
	.id = SM8350_MASTER_PCIE_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde xm_pcie3_1 = {
	.name = "xm_pcie3_1",
	.id = SM8350_MASTER_PCIE_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde xm_qdss_etr = {
	.name = "xm_qdss_etr",
	.id = SM8350_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde xm_sdc2 = {
	.name = "xm_sdc2",
	.id = SM8350_MASTER_SDCC_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde xm_ufs_card = {
	.name = "xm_ufs_card",
	.id = SM8350_MASTER_UFS_CARD,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qnm_gemanalc_canalc = {
	.name = "qnm_gemanalc_canalc",
	.id = SM8350_MASTER_GEM_ANALC_CANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 56,
	.links = { SM8350_SLAVE_AHB2PHY_SOUTH,
		   SM8350_SLAVE_AHB2PHY_ANALRTH,
		   SM8350_SLAVE_AOSS,
		   SM8350_SLAVE_APPSS,
		   SM8350_SLAVE_CAMERA_CFG,
		   SM8350_SLAVE_CLK_CTL,
		   SM8350_SLAVE_CDSP_CFG,
		   SM8350_SLAVE_RBCPR_CX_CFG,
		   SM8350_SLAVE_RBCPR_MMCX_CFG,
		   SM8350_SLAVE_RBCPR_MX_CFG,
		   SM8350_SLAVE_CRYPTO_0_CFG,
		   SM8350_SLAVE_CX_RDPM,
		   SM8350_SLAVE_DCC_CFG,
		   SM8350_SLAVE_DISPLAY_CFG,
		   SM8350_SLAVE_GFX3D_CFG,
		   SM8350_SLAVE_HWKM,
		   SM8350_SLAVE_IMEM_CFG,
		   SM8350_SLAVE_IPA_CFG,
		   SM8350_SLAVE_IPC_ROUTER_CFG,
		   SM8350_SLAVE_LPASS,
		   SM8350_SLAVE_CANALC_MSS,
		   SM8350_SLAVE_MX_RDPM,
		   SM8350_SLAVE_PCIE_0_CFG,
		   SM8350_SLAVE_PCIE_1_CFG,
		   SM8350_SLAVE_PDM,
		   SM8350_SLAVE_PIMEM_CFG,
		   SM8350_SLAVE_PKA_WRAPPER_CFG,
		   SM8350_SLAVE_PMU_WRAPPER_CFG,
		   SM8350_SLAVE_QDSS_CFG,
		   SM8350_SLAVE_QSPI_0,
		   SM8350_SLAVE_QUP_0,
		   SM8350_SLAVE_QUP_1,
		   SM8350_SLAVE_QUP_2,
		   SM8350_SLAVE_SDCC_2,
		   SM8350_SLAVE_SDCC_4,
		   SM8350_SLAVE_SECURITY,
		   SM8350_SLAVE_SPSS_CFG,
		   SM8350_SLAVE_TCSR,
		   SM8350_SLAVE_TLMM,
		   SM8350_SLAVE_UFS_CARD_CFG,
		   SM8350_SLAVE_UFS_MEM_CFG,
		   SM8350_SLAVE_USB3_0,
		   SM8350_SLAVE_USB3_1,
		   SM8350_SLAVE_VENUS_CFG,
		   SM8350_SLAVE_VSENSE_CTRL_CFG,
		   SM8350_SLAVE_A1ANALC_CFG,
		   SM8350_SLAVE_A2ANALC_CFG,
		   SM8350_SLAVE_DDRSS_CFG,
		   SM8350_SLAVE_CANALC_MANALC_CFG,
		   SM8350_SLAVE_SANALC_CFG,
		   SM8350_SLAVE_BOOT_IMEM,
		   SM8350_SLAVE_IMEM,
		   SM8350_SLAVE_PIMEM,
		   SM8350_SLAVE_SERVICE_CANALC,
		   SM8350_SLAVE_QDSS_STM,
		   SM8350_SLAVE_TCU
	},
};

static struct qcom_icc_analde qnm_gemanalc_pcie = {
	.name = "qnm_gemanalc_pcie",
	.id = SM8350_MASTER_GEM_ANALC_PCIE_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM8350_SLAVE_PCIE_0,
		   SM8350_SLAVE_PCIE_1
	},
};

static struct qcom_icc_analde xm_qdss_dap = {
	.name = "xm_qdss_dap",
	.id = SM8350_MASTER_QDSS_DAP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 56,
	.links = { SM8350_SLAVE_AHB2PHY_SOUTH,
		   SM8350_SLAVE_AHB2PHY_ANALRTH,
		   SM8350_SLAVE_AOSS,
		   SM8350_SLAVE_APPSS,
		   SM8350_SLAVE_CAMERA_CFG,
		   SM8350_SLAVE_CLK_CTL,
		   SM8350_SLAVE_CDSP_CFG,
		   SM8350_SLAVE_RBCPR_CX_CFG,
		   SM8350_SLAVE_RBCPR_MMCX_CFG,
		   SM8350_SLAVE_RBCPR_MX_CFG,
		   SM8350_SLAVE_CRYPTO_0_CFG,
		   SM8350_SLAVE_CX_RDPM,
		   SM8350_SLAVE_DCC_CFG,
		   SM8350_SLAVE_DISPLAY_CFG,
		   SM8350_SLAVE_GFX3D_CFG,
		   SM8350_SLAVE_HWKM,
		   SM8350_SLAVE_IMEM_CFG,
		   SM8350_SLAVE_IPA_CFG,
		   SM8350_SLAVE_IPC_ROUTER_CFG,
		   SM8350_SLAVE_LPASS,
		   SM8350_SLAVE_CANALC_MSS,
		   SM8350_SLAVE_MX_RDPM,
		   SM8350_SLAVE_PCIE_0_CFG,
		   SM8350_SLAVE_PCIE_1_CFG,
		   SM8350_SLAVE_PDM,
		   SM8350_SLAVE_PIMEM_CFG,
		   SM8350_SLAVE_PKA_WRAPPER_CFG,
		   SM8350_SLAVE_PMU_WRAPPER_CFG,
		   SM8350_SLAVE_QDSS_CFG,
		   SM8350_SLAVE_QSPI_0,
		   SM8350_SLAVE_QUP_0,
		   SM8350_SLAVE_QUP_1,
		   SM8350_SLAVE_QUP_2,
		   SM8350_SLAVE_SDCC_2,
		   SM8350_SLAVE_SDCC_4,
		   SM8350_SLAVE_SECURITY,
		   SM8350_SLAVE_SPSS_CFG,
		   SM8350_SLAVE_TCSR,
		   SM8350_SLAVE_TLMM,
		   SM8350_SLAVE_UFS_CARD_CFG,
		   SM8350_SLAVE_UFS_MEM_CFG,
		   SM8350_SLAVE_USB3_0,
		   SM8350_SLAVE_USB3_1,
		   SM8350_SLAVE_VENUS_CFG,
		   SM8350_SLAVE_VSENSE_CTRL_CFG,
		   SM8350_SLAVE_A1ANALC_CFG,
		   SM8350_SLAVE_A2ANALC_CFG,
		   SM8350_SLAVE_DDRSS_CFG,
		   SM8350_SLAVE_CANALC_MANALC_CFG,
		   SM8350_SLAVE_SANALC_CFG,
		   SM8350_SLAVE_BOOT_IMEM,
		   SM8350_SLAVE_IMEM,
		   SM8350_SLAVE_PIMEM,
		   SM8350_SLAVE_SERVICE_CANALC,
		   SM8350_SLAVE_QDSS_STM,
		   SM8350_SLAVE_TCU
	},
};

static struct qcom_icc_analde qnm_canalc_dc_analc = {
	.name = "qnm_canalc_dc_analc",
	.id = SM8350_MASTER_CANALC_DC_ANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.links = { SM8350_SLAVE_LLCC_CFG,
		   SM8350_SLAVE_GEM_ANALC_CFG
	},
};

static struct qcom_icc_analde alm_gpu_tcu = {
	.name = "alm_gpu_tcu",
	.id = SM8350_MASTER_GPU_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM8350_SLAVE_GEM_ANALC_CANALC,
		   SM8350_SLAVE_LLCC
	},
};

static struct qcom_icc_analde alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.id = SM8350_MASTER_SYS_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM8350_SLAVE_GEM_ANALC_CANALC,
		   SM8350_SLAVE_LLCC
	},
};

static struct qcom_icc_analde chm_apps = {
	.name = "chm_apps",
	.id = SM8350_MASTER_APPSS_PROC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 3,
	.links = { SM8350_SLAVE_GEM_ANALC_CANALC,
		   SM8350_SLAVE_LLCC,
		   SM8350_SLAVE_MEM_ANALC_PCIE_SANALC
	},
};

static struct qcom_icc_analde qnm_cmpanalc = {
	.name = "qnm_cmpanalc",
	.id = SM8350_MASTER_COMPUTE_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM8350_SLAVE_GEM_ANALC_CANALC,
		   SM8350_SLAVE_LLCC
	},
};

static struct qcom_icc_analde qnm_gemanalc_cfg = {
	.name = "qnm_gemanalc_cfg",
	.id = SM8350_MASTER_GEM_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 5,
	.links = { SM8350_SLAVE_MSS_PROC_MS_MPU_CFG,
		   SM8350_SLAVE_MCDMA_MS_MPU_CFG,
		   SM8350_SLAVE_SERVICE_GEM_ANALC_1,
		   SM8350_SLAVE_SERVICE_GEM_ANALC_2,
		   SM8350_SLAVE_SERVICE_GEM_ANALC
	},
};

static struct qcom_icc_analde qnm_gpu = {
	.name = "qnm_gpu",
	.id = SM8350_MASTER_GFX3D,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM8350_SLAVE_GEM_ANALC_CANALC,
		   SM8350_SLAVE_LLCC
	},
};

static struct qcom_icc_analde qnm_manalc_hf = {
	.name = "qnm_manalc_hf",
	.id = SM8350_MASTER_MANALC_HF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_manalc_sf = {
	.name = "qnm_manalc_sf",
	.id = SM8350_MASTER_MANALC_SF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM8350_SLAVE_GEM_ANALC_CANALC,
		   SM8350_SLAVE_LLCC
	},
};

static struct qcom_icc_analde qnm_pcie = {
	.name = "qnm_pcie",
	.id = SM8350_MASTER_AANALC_PCIE_GEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 2,
	.links = { SM8350_SLAVE_GEM_ANALC_CANALC,
		   SM8350_SLAVE_LLCC
	},
};

static struct qcom_icc_analde qnm_sanalc_gc = {
	.name = "qnm_sanalc_gc",
	.id = SM8350_MASTER_SANALC_GC_MEM_ANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_sanalc_sf = {
	.name = "qnm_sanalc_sf",
	.id = SM8350_MASTER_SANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { SM8350_SLAVE_GEM_ANALC_CANALC,
		   SM8350_SLAVE_LLCC,
		   SM8350_SLAVE_MEM_ANALC_PCIE_SANALC
	},
};

static struct qcom_icc_analde qhm_config_analc = {
	.name = "qhm_config_analc",
	.id = SM8350_MASTER_CANALC_LPASS_AG_ANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 6,
	.links = { SM8350_SLAVE_LPASS_CORE_CFG,
		   SM8350_SLAVE_LPASS_LPI_CFG,
		   SM8350_SLAVE_LPASS_MPU_CFG,
		   SM8350_SLAVE_LPASS_TOP_CFG,
		   SM8350_SLAVE_SERVICES_LPASS_AML_ANALC,
		   SM8350_SLAVE_SERVICE_LPASS_AG_ANALC
	},
};

static struct qcom_icc_analde llcc_mc = {
	.name = "llcc_mc",
	.id = SM8350_MASTER_LLCC,
	.channels = 4,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_EBI1 },
};

static struct qcom_icc_analde qnm_camanalc_hf = {
	.name = "qnm_camanalc_hf",
	.id = SM8350_MASTER_CAMANALC_HF,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_camanalc_icp = {
	.name = "qnm_camanalc_icp",
	.id = SM8350_MASTER_CAMANALC_ICP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_camanalc_sf = {
	.name = "qnm_camanalc_sf",
	.id = SM8350_MASTER_CAMANALC_SF,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_manalc_cfg = {
	.name = "qnm_manalc_cfg",
	.id = SM8350_MASTER_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_SERVICE_MANALC },
};

static struct qcom_icc_analde qnm_video0 = {
	.name = "qnm_video0",
	.id = SM8350_MASTER_VIDEO_P0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_video1 = {
	.name = "qnm_video1",
	.id = SM8350_MASTER_VIDEO_P1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_video_cvp = {
	.name = "qnm_video_cvp",
	.id = SM8350_MASTER_VIDEO_PROC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_mdp0 = {
	.name = "qxm_mdp0",
	.id = SM8350_MASTER_MDP0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_mdp1 = {
	.name = "qxm_mdp1",
	.id = SM8350_MASTER_MDP1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_rot = {
	.name = "qxm_rot",
	.id = SM8350_MASTER_ROTATOR,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qhm_nsp_analc_config = {
	.name = "qhm_nsp_analc_config",
	.id = SM8350_MASTER_CDSP_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_SERVICE_NSP_ANALC },
};

static struct qcom_icc_analde qxm_nsp = {
	.name = "qxm_nsp",
	.id = SM8350_MASTER_CDSP_PROC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_CDSP_MEM_ANALC },
};

static struct qcom_icc_analde qnm_aggre1_analc = {
	.name = "qnm_aggre1_analc",
	.id = SM8350_MASTER_A1ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8350_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qnm_aggre2_analc = {
	.name = "qnm_aggre2_analc",
	.id = SM8350_MASTER_A2ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8350_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qnm_sanalc_cfg = {
	.name = "qnm_sanalc_cfg",
	.id = SM8350_MASTER_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_SERVICE_SANALC },
};

static struct qcom_icc_analde qxm_pimem = {
	.name = "qxm_pimem",
	.id = SM8350_MASTER_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_SANALC_GEM_ANALC_GC },
};

static struct qcom_icc_analde xm_gic = {
	.name = "xm_gic",
	.id = SM8350_MASTER_GIC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_SANALC_GEM_ANALC_GC },
};

static struct qcom_icc_analde qnm_manalc_hf_disp = {
	.name = "qnm_manalc_hf_disp",
	.id = SM8350_MASTER_MANALC_HF_MEM_ANALC_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_LLCC_DISP },
};

static struct qcom_icc_analde qnm_manalc_sf_disp = {
	.name = "qnm_manalc_sf_disp",
	.id = SM8350_MASTER_MANALC_SF_MEM_ANALC_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_LLCC_DISP },
};

static struct qcom_icc_analde llcc_mc_disp = {
	.name = "llcc_mc_disp",
	.id = SM8350_MASTER_LLCC_DISP,
	.channels = 4,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_EBI1_DISP },
};

static struct qcom_icc_analde qxm_mdp0_disp = {
	.name = "qxm_mdp0_disp",
	.id = SM8350_MASTER_MDP0_DISP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MANALC_HF_MEM_ANALC_DISP },
};

static struct qcom_icc_analde qxm_mdp1_disp = {
	.name = "qxm_mdp1_disp",
	.id = SM8350_MASTER_MDP1_DISP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MANALC_HF_MEM_ANALC_DISP },
};

static struct qcom_icc_analde qxm_rot_disp = {
	.name = "qxm_rot_disp",
	.id = SM8350_MASTER_ROTATOR_DISP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MANALC_SF_MEM_ANALC_DISP },
};

static struct qcom_icc_analde qns_a1analc_sanalc = {
	.name = "qns_a1analc_sanalc",
	.id = SM8350_SLAVE_A1ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8350_MASTER_A1ANALC_SANALC },
};

static struct qcom_icc_analde srvc_aggre1_analc = {
	.name = "srvc_aggre1_analc",
	.id = SM8350_SLAVE_SERVICE_A1ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_a2analc_sanalc = {
	.name = "qns_a2analc_sanalc",
	.id = SM8350_SLAVE_A2ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8350_MASTER_A2ANALC_SANALC },
};

static struct qcom_icc_analde qns_pcie_mem_analc = {
	.name = "qns_pcie_mem_analc",
	.id = SM8350_SLAVE_AANALC_PCIE_GEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8350_MASTER_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde srvc_aggre2_analc = {
	.name = "srvc_aggre2_analc",
	.id = SM8350_SLAVE_SERVICE_A2ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ahb2phy0 = {
	.name = "qhs_ahb2phy0",
	.id = SM8350_SLAVE_AHB2PHY_SOUTH,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ahb2phy1 = {
	.name = "qhs_ahb2phy1",
	.id = SM8350_SLAVE_AHB2PHY_ANALRTH,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_aoss = {
	.name = "qhs_aoss",
	.id = SM8350_SLAVE_AOSS,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_apss = {
	.name = "qhs_apss",
	.id = SM8350_SLAVE_APPSS,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qhs_camera_cfg = {
	.name = "qhs_camera_cfg",
	.id = SM8350_SLAVE_CAMERA_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SM8350_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_compute_cfg = {
	.name = "qhs_compute_cfg",
	.id = SM8350_SLAVE_CDSP_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = SM8350_SLAVE_RBCPR_CX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cpr_mmcx = {
	.name = "qhs_cpr_mmcx",
	.id = SM8350_SLAVE_RBCPR_MMCX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cpr_mx = {
	.name = "qhs_cpr_mx",
	.id = SM8350_SLAVE_RBCPR_MX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = SM8350_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cx_rdpm = {
	.name = "qhs_cx_rdpm",
	.id = SM8350_SLAVE_CX_RDPM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_dcc_cfg = {
	.name = "qhs_dcc_cfg",
	.id = SM8350_SLAVE_DCC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_display_cfg = {
	.name = "qhs_display_cfg",
	.id = SM8350_SLAVE_DISPLAY_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.id = SM8350_SLAVE_GFX3D_CFG,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qhs_hwkm = {
	.name = "qhs_hwkm",
	.id = SM8350_SLAVE_HWKM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SM8350_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ipa = {
	.name = "qhs_ipa",
	.id = SM8350_SLAVE_IPA_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ipc_router = {
	.name = "qhs_ipc_router",
	.id = SM8350_SLAVE_IPC_ROUTER_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_lpass_cfg = {
	.name = "qhs_lpass_cfg",
	.id = SM8350_SLAVE_LPASS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_MASTER_CANALC_LPASS_AG_ANALC },
};

static struct qcom_icc_analde qhs_mss_cfg = {
	.name = "qhs_mss_cfg",
	.id = SM8350_SLAVE_CANALC_MSS,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_mx_rdpm = {
	.name = "qhs_mx_rdpm",
	.id = SM8350_SLAVE_MX_RDPM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pcie0_cfg = {
	.name = "qhs_pcie0_cfg",
	.id = SM8350_SLAVE_PCIE_0_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pcie1_cfg = {
	.name = "qhs_pcie1_cfg",
	.id = SM8350_SLAVE_PCIE_1_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pdm = {
	.name = "qhs_pdm",
	.id = SM8350_SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = SM8350_SLAVE_PIMEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pka_wrapper_cfg = {
	.name = "qhs_pka_wrapper_cfg",
	.id = SM8350_SLAVE_PKA_WRAPPER_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pmu_wrapper_cfg = {
	.name = "qhs_pmu_wrapper_cfg",
	.id = SM8350_SLAVE_PMU_WRAPPER_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SM8350_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qspi = {
	.name = "qhs_qspi",
	.id = SM8350_SLAVE_QSPI_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qup0 = {
	.name = "qhs_qup0",
	.id = SM8350_SLAVE_QUP_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qup1 = {
	.name = "qhs_qup1",
	.id = SM8350_SLAVE_QUP_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qup2 = {
	.name = "qhs_qup2",
	.id = SM8350_SLAVE_QUP_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = SM8350_SLAVE_SDCC_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_sdc4 = {
	.name = "qhs_sdc4",
	.id = SM8350_SLAVE_SDCC_4,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_security = {
	.name = "qhs_security",
	.id = SM8350_SLAVE_SECURITY,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_spss_cfg = {
	.name = "qhs_spss_cfg",
	.id = SM8350_SLAVE_SPSS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SM8350_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tlmm = {
	.name = "qhs_tlmm",
	.id = SM8350_SLAVE_TLMM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ufs_card_cfg = {
	.name = "qhs_ufs_card_cfg",
	.id = SM8350_SLAVE_UFS_CARD_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.id = SM8350_SLAVE_UFS_MEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_usb3_0 = {
	.name = "qhs_usb3_0",
	.id = SM8350_SLAVE_USB3_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_usb3_1 = {
	.name = "qhs_usb3_1",
	.id = SM8350_SLAVE_USB3_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = SM8350_SLAVE_VENUS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.id = SM8350_SLAVE_VSENSE_CTRL_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_a1_analc_cfg = {
	.name = "qns_a1_analc_cfg",
	.id = SM8350_SLAVE_A1ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_a2_analc_cfg = {
	.name = "qns_a2_analc_cfg",
	.id = SM8350_SLAVE_A2ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_ddrss_cfg = {
	.name = "qns_ddrss_cfg",
	.id = SM8350_SLAVE_DDRSS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_manalc_cfg = {
	.name = "qns_manalc_cfg",
	.id = SM8350_SLAVE_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_sanalc_cfg = {
	.name = "qns_sanalc_cfg",
	.id = SM8350_SLAVE_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qxs_boot_imem = {
	.name = "qxs_boot_imem",
	.id = SM8350_SLAVE_BOOT_IMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qxs_imem = {
	.name = "qxs_imem",
	.id = SM8350_SLAVE_IMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qxs_pimem = {
	.name = "qxs_pimem",
	.id = SM8350_SLAVE_PIMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde srvc_canalc = {
	.name = "srvc_canalc",
	.id = SM8350_SLAVE_SERVICE_CANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde xs_pcie_0 = {
	.name = "xs_pcie_0",
	.id = SM8350_SLAVE_PCIE_0,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde xs_pcie_1 = {
	.name = "xs_pcie_1",
	.id = SM8350_SLAVE_PCIE_1,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SM8350_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SM8350_SLAVE_TCU,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qhs_llcc = {
	.name = "qhs_llcc",
	.id = SM8350_SLAVE_LLCC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_gemanalc = {
	.name = "qns_gemanalc",
	.id = SM8350_SLAVE_GEM_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_mdsp_ms_mpu_cfg = {
	.name = "qhs_mdsp_ms_mpu_cfg",
	.id = SM8350_SLAVE_MSS_PROC_MS_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_modem_ms_mpu_cfg = {
	.name = "qhs_modem_ms_mpu_cfg",
	.id = SM8350_SLAVE_MCDMA_MS_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_gem_analc_canalc = {
	.name = "qns_gem_analc_canalc",
	.id = SM8350_SLAVE_GEM_ANALC_CANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8350_MASTER_GEM_ANALC_CANALC },
};

static struct qcom_icc_analde qns_llcc = {
	.name = "qns_llcc",
	.id = SM8350_SLAVE_LLCC,
	.channels = 4,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8350_MASTER_LLCC },
};

static struct qcom_icc_analde qns_pcie = {
	.name = "qns_pcie",
	.id = SM8350_SLAVE_MEM_ANALC_PCIE_SANALC,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde srvc_even_gemanalc = {
	.name = "srvc_even_gemanalc",
	.id = SM8350_SLAVE_SERVICE_GEM_ANALC_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde srvc_odd_gemanalc = {
	.name = "srvc_odd_gemanalc",
	.id = SM8350_SLAVE_SERVICE_GEM_ANALC_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde srvc_sys_gemanalc = {
	.name = "srvc_sys_gemanalc",
	.id = SM8350_SLAVE_SERVICE_GEM_ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_lpass_core = {
	.name = "qhs_lpass_core",
	.id = SM8350_SLAVE_LPASS_CORE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_lpass_lpi = {
	.name = "qhs_lpass_lpi",
	.id = SM8350_SLAVE_LPASS_LPI_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_lpass_mpu = {
	.name = "qhs_lpass_mpu",
	.id = SM8350_SLAVE_LPASS_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_lpass_top = {
	.name = "qhs_lpass_top",
	.id = SM8350_SLAVE_LPASS_TOP_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde srvc_niu_aml_analc = {
	.name = "srvc_niu_aml_analc",
	.id = SM8350_SLAVE_SERVICES_LPASS_AML_ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde srvc_niu_lpass_aganalc = {
	.name = "srvc_niu_lpass_aganalc",
	.id = SM8350_SLAVE_SERVICE_LPASS_AG_ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde ebi = {
	.name = "ebi",
	.id = SM8350_SLAVE_EBI1,
	.channels = 4,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_mem_analc_hf = {
	.name = "qns_mem_analc_hf",
	.id = SM8350_SLAVE_MANALC_HF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_MASTER_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qns_mem_analc_sf = {
	.name = "qns_mem_analc_sf",
	.id = SM8350_SLAVE_MANALC_SF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_MASTER_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde srvc_manalc = {
	.name = "srvc_manalc",
	.id = SM8350_SLAVE_SERVICE_MANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_nsp_gemanalc = {
	.name = "qns_nsp_gemanalc",
	.id = SM8350_SLAVE_CDSP_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_MASTER_COMPUTE_ANALC },
};

static struct qcom_icc_analde service_nsp_analc = {
	.name = "service_nsp_analc",
	.id = SM8350_SLAVE_SERVICE_NSP_ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_gemanalc_gc = {
	.name = "qns_gemanalc_gc",
	.id = SM8350_SLAVE_SANALC_GEM_ANALC_GC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_MASTER_SANALC_GC_MEM_ANALC },
};

static struct qcom_icc_analde qns_gemanalc_sf = {
	.name = "qns_gemanalc_sf",
	.id = SM8350_SLAVE_SANALC_GEM_ANALC_SF,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8350_MASTER_SANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde srvc_sanalc = {
	.name = "srvc_sanalc",
	.id = SM8350_SLAVE_SERVICE_SANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_llcc_disp = {
	.name = "qns_llcc_disp",
	.id = SM8350_SLAVE_LLCC_DISP,
	.channels = 4,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8350_MASTER_LLCC_DISP },
};

static struct qcom_icc_analde ebi_disp = {
	.name = "ebi_disp",
	.id = SM8350_SLAVE_EBI1_DISP,
	.channels = 4,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_mem_analc_hf_disp = {
	.name = "qns_mem_analc_hf_disp",
	.id = SM8350_SLAVE_MANALC_HF_MEM_ANALC_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_MASTER_MANALC_HF_MEM_ANALC_DISP },
};

static struct qcom_icc_analde qns_mem_analc_sf_disp = {
	.name = "qns_mem_analc_sf_disp",
	.id = SM8350_SLAVE_MANALC_SF_MEM_ANALC_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_MASTER_MANALC_SF_MEM_ANALC_DISP },
};

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
	.enable_mask = BIT(3),
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &ebi },
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.keepalive = true,
	.num_analdes = 2,
	.analdes = { &qnm_gemanalc_canalc, &qnm_gemanalc_pcie },
};

static struct qcom_icc_bcm bcm_cn1 = {
	.name = "CN1",
	.keepalive = false,
	.num_analdes = 47,
	.analdes = { &xm_qdss_dap,
		   &qhs_ahb2phy0,
		   &qhs_ahb2phy1,
		   &qhs_aoss,
		   &qhs_apss,
		   &qhs_camera_cfg,
		   &qhs_clk_ctl,
		   &qhs_compute_cfg,
		   &qhs_cpr_cx,
		   &qhs_cpr_mmcx,
		   &qhs_cpr_mx,
		   &qhs_crypto0_cfg,
		   &qhs_cx_rdpm,
		   &qhs_dcc_cfg,
		   &qhs_display_cfg,
		   &qhs_gpuss_cfg,
		   &qhs_hwkm,
		   &qhs_imem_cfg,
		   &qhs_ipa,
		   &qhs_ipc_router,
		   &qhs_mss_cfg,
		   &qhs_mx_rdpm,
		   &qhs_pcie0_cfg,
		   &qhs_pcie1_cfg,
		   &qhs_pimem_cfg,
		   &qhs_pka_wrapper_cfg,
		   &qhs_pmu_wrapper_cfg,
		   &qhs_qdss_cfg,
		   &qhs_qup0,
		   &qhs_qup1,
		   &qhs_qup2,
		   &qhs_security,
		   &qhs_spss_cfg,
		   &qhs_tcsr,
		   &qhs_tlmm,
		   &qhs_ufs_card_cfg,
		   &qhs_ufs_mem_cfg,
		   &qhs_usb3_0,
		   &qhs_usb3_1,
		   &qhs_venus_cfg,
		   &qhs_vsense_ctrl_cfg,
		   &qns_a1_analc_cfg,
		   &qns_a2_analc_cfg,
		   &qns_ddrss_cfg,
		   &qns_manalc_cfg,
		   &qns_sanalc_cfg,
		   &srvc_canalc
	},
};

static struct qcom_icc_bcm bcm_cn2 = {
	.name = "CN2",
	.keepalive = false,
	.num_analdes = 5,
	.analdes = { &qhs_lpass_cfg, &qhs_pdm, &qhs_qspi, &qhs_sdc2, &qhs_sdc4 },
};

static struct qcom_icc_bcm bcm_co0 = {
	.name = "CO0",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qns_nsp_gemanalc },
};

static struct qcom_icc_bcm bcm_co3 = {
	.name = "CO3",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qxm_nsp },
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
	.keepalive = false,
	.num_analdes = 3,
	.analdes = { &qnm_camanalc_hf, &qxm_mdp0, &qxm_mdp1 },
};

static struct qcom_icc_bcm bcm_mm4 = {
	.name = "MM4",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qns_mem_analc_sf },
};

static struct qcom_icc_bcm bcm_mm5 = {
	.name = "MM5",
	.keepalive = false,
	.num_analdes = 6,
	.analdes = { &qnm_camanalc_icp,
		   &qnm_camanalc_sf,
		   &qnm_video0,
		   &qnm_video1,
		   &qnm_video_cvp,
		   &qxm_rot
	},
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_sh2 = {
	.name = "SH2",
	.keepalive = false,
	.num_analdes = 2,
	.analdes = { &alm_gpu_tcu, &alm_sys_tcu },
};

static struct qcom_icc_bcm bcm_sh3 = {
	.name = "SH3",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qnm_cmpanalc },
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

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qns_gemanalc_gc },
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
	.analdes = { &xm_pcie3_0 },
};

static struct qcom_icc_bcm bcm_sn6 = {
	.name = "SN6",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &xm_pcie3_1 },
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

static struct qcom_icc_bcm bcm_sn14 = {
	.name = "SN14",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qns_pcie_mem_analc },
};

static struct qcom_icc_bcm bcm_acv_disp = {
	.name = "ACV",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &ebi_disp },
};

static struct qcom_icc_bcm bcm_mc0_disp = {
	.name = "MC0",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &ebi_disp },
};

static struct qcom_icc_bcm bcm_mm0_disp = {
	.name = "MM0",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qns_mem_analc_hf_disp },
};

static struct qcom_icc_bcm bcm_mm1_disp = {
	.name = "MM1",
	.keepalive = false,
	.num_analdes = 2,
	.analdes = { &qxm_mdp0_disp, &qxm_mdp1_disp },
};

static struct qcom_icc_bcm bcm_mm4_disp = {
	.name = "MM4",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qns_mem_analc_sf_disp },
};

static struct qcom_icc_bcm bcm_mm5_disp = {
	.name = "MM5",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qxm_rot_disp },
};

static struct qcom_icc_bcm bcm_sh0_disp = {
	.name = "SH0",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qns_llcc_disp },
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
	[MASTER_USB3_1] = &xm_usb3_1,
	[SLAVE_A1ANALC_SANALC] = &qns_a1analc_sanalc,
	[SLAVE_SERVICE_A1ANALC] = &srvc_aggre1_analc,
};

static const struct qcom_icc_desc sm8350_aggre1_analc = {
	.analdes = aggre1_analc_analdes,
	.num_analdes = ARRAY_SIZE(aggre1_analc_analdes),
	.bcms = aggre1_analc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_analc_bcms),
};

static struct qcom_icc_bcm * const aggre2_analc_bcms[] = {
	&bcm_ce0,
	&bcm_sn5,
	&bcm_sn6,
	&bcm_sn14,
};

static struct qcom_icc_analde * const aggre2_analc_analdes[] = {
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_QUP_2] = &qhm_qup2,
	[MASTER_A2ANALC_CFG] = &qnm_a2analc_cfg,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_PCIE_0] = &xm_pcie3_0,
	[MASTER_PCIE_1] = &xm_pcie3_1,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_UFS_CARD] = &xm_ufs_card,
	[SLAVE_A2ANALC_SANALC] = &qns_a2analc_sanalc,
	[SLAVE_AANALC_PCIE_GEM_ANALC] = &qns_pcie_mem_analc,
	[SLAVE_SERVICE_A2ANALC] = &srvc_aggre2_analc,
};

static const struct qcom_icc_desc sm8350_aggre2_analc = {
	.analdes = aggre2_analc_analdes,
	.num_analdes = ARRAY_SIZE(aggre2_analc_analdes),
	.bcms = aggre2_analc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_analc_bcms),
};

static struct qcom_icc_bcm * const config_analc_bcms[] = {
	&bcm_cn0,
	&bcm_cn1,
	&bcm_cn2,
	&bcm_sn3,
	&bcm_sn4,
};

static struct qcom_icc_analde * const config_analc_analdes[] = {
	[MASTER_GEM_ANALC_CANALC] = &qnm_gemanalc_canalc,
	[MASTER_GEM_ANALC_PCIE_SANALC] = &qnm_gemanalc_pcie,
	[MASTER_QDSS_DAP] = &xm_qdss_dap,
	[SLAVE_AHB2PHY_SOUTH] = &qhs_ahb2phy0,
	[SLAVE_AHB2PHY_ANALRTH] = &qhs_ahb2phy1,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_APPSS] = &qhs_apss,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CDSP_CFG] = &qhs_compute_cfg,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MMCX_CFG] = &qhs_cpr_mmcx,
	[SLAVE_RBCPR_MX_CFG] = &qhs_cpr_mx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_CX_RDPM] = &qhs_cx_rdpm,
	[SLAVE_DCC_CFG] = &qhs_dcc_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_GFX3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_HWKM] = &qhs_hwkm,
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
	[SLAVE_PKA_WRAPPER_CFG] = &qhs_pka_wrapper_cfg,
	[SLAVE_PMU_WRAPPER_CFG] = &qhs_pmu_wrapper_cfg,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QSPI_0] = &qhs_qspi,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_QUP_2] = &qhs_qup2,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SDCC_4] = &qhs_sdc4,
	[SLAVE_SECURITY] = &qhs_security,
	[SLAVE_SPSS_CFG] = &qhs_spss_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_UFS_CARD_CFG] = &qhs_ufs_card_cfg,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB3_0] = &qhs_usb3_0,
	[SLAVE_USB3_1] = &qhs_usb3_1,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_A1ANALC_CFG] = &qns_a1_analc_cfg,
	[SLAVE_A2ANALC_CFG] = &qns_a2_analc_cfg,
	[SLAVE_DDRSS_CFG] = &qns_ddrss_cfg,
	[SLAVE_CANALC_MANALC_CFG] = &qns_manalc_cfg,
	[SLAVE_SANALC_CFG] = &qns_sanalc_cfg,
	[SLAVE_BOOT_IMEM] = &qxs_boot_imem,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SERVICE_CANALC] = &srvc_canalc,
	[SLAVE_PCIE_0] = &xs_pcie_0,
	[SLAVE_PCIE_1] = &xs_pcie_1,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sm8350_config_analc = {
	.analdes = config_analc_analdes,
	.num_analdes = ARRAY_SIZE(config_analc_analdes),
	.bcms = config_analc_bcms,
	.num_bcms = ARRAY_SIZE(config_analc_bcms),
};

static struct qcom_icc_bcm * const dc_analc_bcms[] = {
};

static struct qcom_icc_analde * const dc_analc_analdes[] = {
	[MASTER_CANALC_DC_ANALC] = &qnm_canalc_dc_analc,
	[SLAVE_LLCC_CFG] = &qhs_llcc,
	[SLAVE_GEM_ANALC_CFG] = &qns_gemanalc,
};

static const struct qcom_icc_desc sm8350_dc_analc = {
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
	&bcm_sh0_disp,
};

static struct qcom_icc_analde * const gem_analc_analdes[] = {
	[MASTER_GPU_TCU] = &alm_gpu_tcu,
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_APPSS_PROC] = &chm_apps,
	[MASTER_COMPUTE_ANALC] = &qnm_cmpanalc,
	[MASTER_GEM_ANALC_CFG] = &qnm_gemanalc_cfg,
	[MASTER_GFX3D] = &qnm_gpu,
	[MASTER_MANALC_HF_MEM_ANALC] = &qnm_manalc_hf,
	[MASTER_MANALC_SF_MEM_ANALC] = &qnm_manalc_sf,
	[MASTER_AANALC_PCIE_GEM_ANALC] = &qnm_pcie,
	[MASTER_SANALC_GC_MEM_ANALC] = &qnm_sanalc_gc,
	[MASTER_SANALC_SF_MEM_ANALC] = &qnm_sanalc_sf,
	[SLAVE_MSS_PROC_MS_MPU_CFG] = &qhs_mdsp_ms_mpu_cfg,
	[SLAVE_MCDMA_MS_MPU_CFG] = &qhs_modem_ms_mpu_cfg,
	[SLAVE_GEM_ANALC_CANALC] = &qns_gem_analc_canalc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_MEM_ANALC_PCIE_SANALC] = &qns_pcie,
	[SLAVE_SERVICE_GEM_ANALC_1] = &srvc_even_gemanalc,
	[SLAVE_SERVICE_GEM_ANALC_2] = &srvc_odd_gemanalc,
	[SLAVE_SERVICE_GEM_ANALC] = &srvc_sys_gemanalc,
	[MASTER_MANALC_HF_MEM_ANALC_DISP] = &qnm_manalc_hf_disp,
	[MASTER_MANALC_SF_MEM_ANALC_DISP] = &qnm_manalc_sf_disp,
	[SLAVE_LLCC_DISP] = &qns_llcc_disp,
};

static const struct qcom_icc_desc sm8350_gem_analc = {
	.analdes = gem_analc_analdes,
	.num_analdes = ARRAY_SIZE(gem_analc_analdes),
	.bcms = gem_analc_bcms,
	.num_bcms = ARRAY_SIZE(gem_analc_bcms),
};

static struct qcom_icc_bcm * const lpass_ag_analc_bcms[] = {
};

static struct qcom_icc_analde * const lpass_ag_analc_analdes[] = {
	[MASTER_CANALC_LPASS_AG_ANALC] = &qhm_config_analc,
	[SLAVE_LPASS_CORE_CFG] = &qhs_lpass_core,
	[SLAVE_LPASS_LPI_CFG] = &qhs_lpass_lpi,
	[SLAVE_LPASS_MPU_CFG] = &qhs_lpass_mpu,
	[SLAVE_LPASS_TOP_CFG] = &qhs_lpass_top,
	[SLAVE_SERVICES_LPASS_AML_ANALC] = &srvc_niu_aml_analc,
	[SLAVE_SERVICE_LPASS_AG_ANALC] = &srvc_niu_lpass_aganalc,
};

static const struct qcom_icc_desc sm8350_lpass_ag_analc = {
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

static const struct qcom_icc_desc sm8350_mc_virt = {
	.analdes = mc_virt_analdes,
	.num_analdes = ARRAY_SIZE(mc_virt_analdes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm * const mmss_analc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
	&bcm_mm4,
	&bcm_mm5,
	&bcm_mm0_disp,
	&bcm_mm1_disp,
	&bcm_mm4_disp,
	&bcm_mm5_disp,
};

static struct qcom_icc_analde * const mmss_analc_analdes[] = {
	[MASTER_CAMANALC_HF] = &qnm_camanalc_hf,
	[MASTER_CAMANALC_ICP] = &qnm_camanalc_icp,
	[MASTER_CAMANALC_SF] = &qnm_camanalc_sf,
	[MASTER_CANALC_MANALC_CFG] = &qnm_manalc_cfg,
	[MASTER_VIDEO_P0] = &qnm_video0,
	[MASTER_VIDEO_P1] = &qnm_video1,
	[MASTER_VIDEO_PROC] = &qnm_video_cvp,
	[MASTER_MDP0] = &qxm_mdp0,
	[MASTER_MDP1] = &qxm_mdp1,
	[MASTER_ROTATOR] = &qxm_rot,
	[SLAVE_MANALC_HF_MEM_ANALC] = &qns_mem_analc_hf,
	[SLAVE_MANALC_SF_MEM_ANALC] = &qns_mem_analc_sf,
	[SLAVE_SERVICE_MANALC] = &srvc_manalc,
	[MASTER_MDP0_DISP] = &qxm_mdp0_disp,
	[MASTER_MDP1_DISP] = &qxm_mdp1_disp,
	[MASTER_ROTATOR_DISP] = &qxm_rot_disp,
	[SLAVE_MANALC_HF_MEM_ANALC_DISP] = &qns_mem_analc_hf_disp,
	[SLAVE_MANALC_SF_MEM_ANALC_DISP] = &qns_mem_analc_sf_disp,
};

static const struct qcom_icc_desc sm8350_mmss_analc = {
	.analdes = mmss_analc_analdes,
	.num_analdes = ARRAY_SIZE(mmss_analc_analdes),
	.bcms = mmss_analc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_analc_bcms),
};

static struct qcom_icc_bcm * const nsp_analc_bcms[] = {
	&bcm_co0,
	&bcm_co3,
};

static struct qcom_icc_analde * const nsp_analc_analdes[] = {
	[MASTER_CDSP_ANALC_CFG] = &qhm_nsp_analc_config,
	[MASTER_CDSP_PROC] = &qxm_nsp,
	[SLAVE_CDSP_MEM_ANALC] = &qns_nsp_gemanalc,
	[SLAVE_SERVICE_NSP_ANALC] = &service_nsp_analc,
};

static const struct qcom_icc_desc sm8350_compute_analc = {
	.analdes = nsp_analc_analdes,
	.num_analdes = ARRAY_SIZE(nsp_analc_analdes),
	.bcms = nsp_analc_bcms,
	.num_bcms = ARRAY_SIZE(nsp_analc_bcms),
};

static struct qcom_icc_bcm * const system_analc_bcms[] = {
	&bcm_sn0,
	&bcm_sn2,
	&bcm_sn7,
	&bcm_sn8,
};

static struct qcom_icc_analde * const system_analc_analdes[] = {
	[MASTER_A1ANALC_SANALC] = &qnm_aggre1_analc,
	[MASTER_A2ANALC_SANALC] = &qnm_aggre2_analc,
	[MASTER_SANALC_CFG] = &qnm_sanalc_cfg,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_GIC] = &xm_gic,
	[SLAVE_SANALC_GEM_ANALC_GC] = &qns_gemanalc_gc,
	[SLAVE_SANALC_GEM_ANALC_SF] = &qns_gemanalc_sf,
	[SLAVE_SERVICE_SANALC] = &srvc_sanalc,
};

static const struct qcom_icc_desc sm8350_system_analc = {
	.analdes = system_analc_analdes,
	.num_analdes = ARRAY_SIZE(system_analc_analdes),
	.bcms = system_analc_bcms,
	.num_bcms = ARRAY_SIZE(system_analc_bcms),
};

static const struct of_device_id qanalc_of_match[] = {
	{ .compatible = "qcom,sm8350-aggre1-analc", .data = &sm8350_aggre1_analc},
	{ .compatible = "qcom,sm8350-aggre2-analc", .data = &sm8350_aggre2_analc},
	{ .compatible = "qcom,sm8350-config-analc", .data = &sm8350_config_analc},
	{ .compatible = "qcom,sm8350-dc-analc", .data = &sm8350_dc_analc},
	{ .compatible = "qcom,sm8350-gem-analc", .data = &sm8350_gem_analc},
	{ .compatible = "qcom,sm8350-lpass-ag-analc", .data = &sm8350_lpass_ag_analc},
	{ .compatible = "qcom,sm8350-mc-virt", .data = &sm8350_mc_virt},
	{ .compatible = "qcom,sm8350-mmss-analc", .data = &sm8350_mmss_analc},
	{ .compatible = "qcom,sm8350-compute-analc", .data = &sm8350_compute_analc},
	{ .compatible = "qcom,sm8350-system-analc", .data = &sm8350_system_analc},
	{ }
};
MODULE_DEVICE_TABLE(of, qanalc_of_match);

static struct platform_driver qanalc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove_new = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qanalc-sm8350",
		.of_match_table = qanalc_of_match,
	},
};
module_platform_driver(qanalc_driver);

MODULE_DESCRIPTION("SM8350 AnalC driver");
MODULE_LICENSE("GPL v2");
