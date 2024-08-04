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

static struct qcom_icc_node qhm_qspi = {
	.name = "qhm_qspi",
	.id = SM8350_MASTER_QSPI_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node qhm_qup0 = {
	.name = "qhm_qup0",
	.id = SM8350_MASTER_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qhm_qup1 = {
	.name = "qhm_qup1",
	.id = SM8350_MASTER_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node qhm_qup2 = {
	.name = "qhm_qup2",
	.id = SM8350_MASTER_QUP_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qnm_a1noc_cfg = {
	.name = "qnm_a1noc_cfg",
	.id = SM8350_MASTER_A1NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_SERVICE_A1NOC },
};

static struct qcom_icc_node xm_sdc4 = {
	.name = "xm_sdc4",
	.id = SM8350_MASTER_SDCC_4,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.id = SM8350_MASTER_UFS_MEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node xm_usb3_0 = {
	.name = "xm_usb3_0",
	.id = SM8350_MASTER_USB3_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node xm_usb3_1 = {
	.name = "xm_usb3_1",
	.id = SM8350_MASTER_USB3_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = SM8350_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qnm_a2noc_cfg = {
	.name = "qnm_a2noc_cfg",
	.id = SM8350_MASTER_A2NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_SERVICE_A2NOC },
};

static struct qcom_icc_node qxm_crypto = {
	.name = "qxm_crypto",
	.id = SM8350_MASTER_CRYPTO,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qxm_ipa = {
	.name = "qxm_ipa",
	.id = SM8350_MASTER_IPA,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node xm_pcie3_0 = {
	.name = "xm_pcie3_0",
	.id = SM8350_MASTER_PCIE_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_ANOC_PCIE_GEM_NOC },
};

static struct qcom_icc_node xm_pcie3_1 = {
	.name = "xm_pcie3_1",
	.id = SM8350_MASTER_PCIE_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_ANOC_PCIE_GEM_NOC },
};

static struct qcom_icc_node xm_qdss_etr = {
	.name = "xm_qdss_etr",
	.id = SM8350_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node xm_sdc2 = {
	.name = "xm_sdc2",
	.id = SM8350_MASTER_SDCC_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node xm_ufs_card = {
	.name = "xm_ufs_card",
	.id = SM8350_MASTER_UFS_CARD,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qnm_gemnoc_cnoc = {
	.name = "qnm_gemnoc_cnoc",
	.id = SM8350_MASTER_GEM_NOC_CNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 56,
	.links = { SM8350_SLAVE_AHB2PHY_SOUTH,
		   SM8350_SLAVE_AHB2PHY_NORTH,
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
		   SM8350_SLAVE_CNOC_MSS,
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
		   SM8350_SLAVE_A1NOC_CFG,
		   SM8350_SLAVE_A2NOC_CFG,
		   SM8350_SLAVE_DDRSS_CFG,
		   SM8350_SLAVE_CNOC_MNOC_CFG,
		   SM8350_SLAVE_SNOC_CFG,
		   SM8350_SLAVE_BOOT_IMEM,
		   SM8350_SLAVE_IMEM,
		   SM8350_SLAVE_PIMEM,
		   SM8350_SLAVE_SERVICE_CNOC,
		   SM8350_SLAVE_QDSS_STM,
		   SM8350_SLAVE_TCU
	},
};

static struct qcom_icc_node qnm_gemnoc_pcie = {
	.name = "qnm_gemnoc_pcie",
	.id = SM8350_MASTER_GEM_NOC_PCIE_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM8350_SLAVE_PCIE_0,
		   SM8350_SLAVE_PCIE_1
	},
};

static struct qcom_icc_node xm_qdss_dap = {
	.name = "xm_qdss_dap",
	.id = SM8350_MASTER_QDSS_DAP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 56,
	.links = { SM8350_SLAVE_AHB2PHY_SOUTH,
		   SM8350_SLAVE_AHB2PHY_NORTH,
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
		   SM8350_SLAVE_CNOC_MSS,
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
		   SM8350_SLAVE_A1NOC_CFG,
		   SM8350_SLAVE_A2NOC_CFG,
		   SM8350_SLAVE_DDRSS_CFG,
		   SM8350_SLAVE_CNOC_MNOC_CFG,
		   SM8350_SLAVE_SNOC_CFG,
		   SM8350_SLAVE_BOOT_IMEM,
		   SM8350_SLAVE_IMEM,
		   SM8350_SLAVE_PIMEM,
		   SM8350_SLAVE_SERVICE_CNOC,
		   SM8350_SLAVE_QDSS_STM,
		   SM8350_SLAVE_TCU
	},
};

static struct qcom_icc_node qnm_cnoc_dc_noc = {
	.name = "qnm_cnoc_dc_noc",
	.id = SM8350_MASTER_CNOC_DC_NOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.links = { SM8350_SLAVE_LLCC_CFG,
		   SM8350_SLAVE_GEM_NOC_CFG
	},
};

static struct qcom_icc_node alm_gpu_tcu = {
	.name = "alm_gpu_tcu",
	.id = SM8350_MASTER_GPU_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM8350_SLAVE_GEM_NOC_CNOC,
		   SM8350_SLAVE_LLCC
	},
};

static struct qcom_icc_node alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.id = SM8350_MASTER_SYS_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM8350_SLAVE_GEM_NOC_CNOC,
		   SM8350_SLAVE_LLCC
	},
};

static struct qcom_icc_node chm_apps = {
	.name = "chm_apps",
	.id = SM8350_MASTER_APPSS_PROC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 3,
	.links = { SM8350_SLAVE_GEM_NOC_CNOC,
		   SM8350_SLAVE_LLCC,
		   SM8350_SLAVE_MEM_NOC_PCIE_SNOC
	},
};

static struct qcom_icc_node qnm_cmpnoc = {
	.name = "qnm_cmpnoc",
	.id = SM8350_MASTER_COMPUTE_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM8350_SLAVE_GEM_NOC_CNOC,
		   SM8350_SLAVE_LLCC
	},
};

static struct qcom_icc_node qnm_gemnoc_cfg = {
	.name = "qnm_gemnoc_cfg",
	.id = SM8350_MASTER_GEM_NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 5,
	.links = { SM8350_SLAVE_MSS_PROC_MS_MPU_CFG,
		   SM8350_SLAVE_MCDMA_MS_MPU_CFG,
		   SM8350_SLAVE_SERVICE_GEM_NOC_1,
		   SM8350_SLAVE_SERVICE_GEM_NOC_2,
		   SM8350_SLAVE_SERVICE_GEM_NOC
	},
};

static struct qcom_icc_node qnm_gpu = {
	.name = "qnm_gpu",
	.id = SM8350_MASTER_GFX3D,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM8350_SLAVE_GEM_NOC_CNOC,
		   SM8350_SLAVE_LLCC
	},
};

static struct qcom_icc_node qnm_mnoc_hf = {
	.name = "qnm_mnoc_hf",
	.id = SM8350_MASTER_MNOC_HF_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_mnoc_sf = {
	.name = "qnm_mnoc_sf",
	.id = SM8350_MASTER_MNOC_SF_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM8350_SLAVE_GEM_NOC_CNOC,
		   SM8350_SLAVE_LLCC
	},
};

static struct qcom_icc_node qnm_pcie = {
	.name = "qnm_pcie",
	.id = SM8350_MASTER_ANOC_PCIE_GEM_NOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 2,
	.links = { SM8350_SLAVE_GEM_NOC_CNOC,
		   SM8350_SLAVE_LLCC
	},
};

static struct qcom_icc_node qnm_snoc_gc = {
	.name = "qnm_snoc_gc",
	.id = SM8350_MASTER_SNOC_GC_MEM_NOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_snoc_sf = {
	.name = "qnm_snoc_sf",
	.id = SM8350_MASTER_SNOC_SF_MEM_NOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { SM8350_SLAVE_GEM_NOC_CNOC,
		   SM8350_SLAVE_LLCC,
		   SM8350_SLAVE_MEM_NOC_PCIE_SNOC
	},
};

static struct qcom_icc_node qhm_config_noc = {
	.name = "qhm_config_noc",
	.id = SM8350_MASTER_CNOC_LPASS_AG_NOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 6,
	.links = { SM8350_SLAVE_LPASS_CORE_CFG,
		   SM8350_SLAVE_LPASS_LPI_CFG,
		   SM8350_SLAVE_LPASS_MPU_CFG,
		   SM8350_SLAVE_LPASS_TOP_CFG,
		   SM8350_SLAVE_SERVICES_LPASS_AML_NOC,
		   SM8350_SLAVE_SERVICE_LPASS_AG_NOC
	},
};

static struct qcom_icc_node llcc_mc = {
	.name = "llcc_mc",
	.id = SM8350_MASTER_LLCC,
	.channels = 4,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_EBI1 },
};

static struct qcom_icc_node qnm_camnoc_hf = {
	.name = "qnm_camnoc_hf",
	.id = SM8350_MASTER_CAMNOC_HF,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qnm_camnoc_icp = {
	.name = "qnm_camnoc_icp",
	.id = SM8350_MASTER_CAMNOC_ICP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qnm_camnoc_sf = {
	.name = "qnm_camnoc_sf",
	.id = SM8350_MASTER_CAMNOC_SF,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qnm_mnoc_cfg = {
	.name = "qnm_mnoc_cfg",
	.id = SM8350_MASTER_CNOC_MNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_SERVICE_MNOC },
};

static struct qcom_icc_node qnm_video0 = {
	.name = "qnm_video0",
	.id = SM8350_MASTER_VIDEO_P0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qnm_video1 = {
	.name = "qnm_video1",
	.id = SM8350_MASTER_VIDEO_P1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qnm_video_cvp = {
	.name = "qnm_video_cvp",
	.id = SM8350_MASTER_VIDEO_PROC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qxm_mdp0 = {
	.name = "qxm_mdp0",
	.id = SM8350_MASTER_MDP0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qxm_mdp1 = {
	.name = "qxm_mdp1",
	.id = SM8350_MASTER_MDP1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qxm_rot = {
	.name = "qxm_rot",
	.id = SM8350_MASTER_ROTATOR,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qhm_nsp_noc_config = {
	.name = "qhm_nsp_noc_config",
	.id = SM8350_MASTER_CDSP_NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_SERVICE_NSP_NOC },
};

static struct qcom_icc_node qxm_nsp = {
	.name = "qxm_nsp",
	.id = SM8350_MASTER_CDSP_PROC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_CDSP_MEM_NOC },
};

static struct qcom_icc_node qnm_aggre1_noc = {
	.name = "qnm_aggre1_noc",
	.id = SM8350_MASTER_A1NOC_SNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8350_SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node qnm_aggre2_noc = {
	.name = "qnm_aggre2_noc",
	.id = SM8350_MASTER_A2NOC_SNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8350_SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node qnm_snoc_cfg = {
	.name = "qnm_snoc_cfg",
	.id = SM8350_MASTER_SNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_SERVICE_SNOC },
};

static struct qcom_icc_node qxm_pimem = {
	.name = "qxm_pimem",
	.id = SM8350_MASTER_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_SNOC_GEM_NOC_GC },
};

static struct qcom_icc_node xm_gic = {
	.name = "xm_gic",
	.id = SM8350_MASTER_GIC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_SLAVE_SNOC_GEM_NOC_GC },
};

static struct qcom_icc_node qnm_mnoc_hf_disp = {
	.name = "qnm_mnoc_hf_disp",
	.id = SM8350_MASTER_MNOC_HF_MEM_NOC_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_LLCC_DISP },
};

static struct qcom_icc_node qnm_mnoc_sf_disp = {
	.name = "qnm_mnoc_sf_disp",
	.id = SM8350_MASTER_MNOC_SF_MEM_NOC_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_LLCC_DISP },
};

static struct qcom_icc_node llcc_mc_disp = {
	.name = "llcc_mc_disp",
	.id = SM8350_MASTER_LLCC_DISP,
	.channels = 4,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_SLAVE_EBI1_DISP },
};

static struct qcom_icc_node qxm_mdp0_disp = {
	.name = "qxm_mdp0_disp",
	.id = SM8350_MASTER_MDP0_DISP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MNOC_HF_MEM_NOC_DISP },
};

static struct qcom_icc_node qxm_mdp1_disp = {
	.name = "qxm_mdp1_disp",
	.id = SM8350_MASTER_MDP1_DISP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MNOC_HF_MEM_NOC_DISP },
};

static struct qcom_icc_node qxm_rot_disp = {
	.name = "qxm_rot_disp",
	.id = SM8350_MASTER_ROTATOR_DISP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_SLAVE_MNOC_SF_MEM_NOC_DISP },
};

static struct qcom_icc_node qns_a1noc_snoc = {
	.name = "qns_a1noc_snoc",
	.id = SM8350_SLAVE_A1NOC_SNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8350_MASTER_A1NOC_SNOC },
};

static struct qcom_icc_node srvc_aggre1_noc = {
	.name = "srvc_aggre1_noc",
	.id = SM8350_SLAVE_SERVICE_A1NOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_a2noc_snoc = {
	.name = "qns_a2noc_snoc",
	.id = SM8350_SLAVE_A2NOC_SNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8350_MASTER_A2NOC_SNOC },
};

static struct qcom_icc_node qns_pcie_mem_noc = {
	.name = "qns_pcie_mem_noc",
	.id = SM8350_SLAVE_ANOC_PCIE_GEM_NOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8350_MASTER_ANOC_PCIE_GEM_NOC },
};

static struct qcom_icc_node srvc_aggre2_noc = {
	.name = "srvc_aggre2_noc",
	.id = SM8350_SLAVE_SERVICE_A2NOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ahb2phy0 = {
	.name = "qhs_ahb2phy0",
	.id = SM8350_SLAVE_AHB2PHY_SOUTH,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ahb2phy1 = {
	.name = "qhs_ahb2phy1",
	.id = SM8350_SLAVE_AHB2PHY_NORTH,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_aoss = {
	.name = "qhs_aoss",
	.id = SM8350_SLAVE_AOSS,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_apss = {
	.name = "qhs_apss",
	.id = SM8350_SLAVE_APPSS,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qhs_camera_cfg = {
	.name = "qhs_camera_cfg",
	.id = SM8350_SLAVE_CAMERA_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SM8350_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_compute_cfg = {
	.name = "qhs_compute_cfg",
	.id = SM8350_SLAVE_CDSP_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = SM8350_SLAVE_RBCPR_CX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_cpr_mmcx = {
	.name = "qhs_cpr_mmcx",
	.id = SM8350_SLAVE_RBCPR_MMCX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_cpr_mx = {
	.name = "qhs_cpr_mx",
	.id = SM8350_SLAVE_RBCPR_MX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = SM8350_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_cx_rdpm = {
	.name = "qhs_cx_rdpm",
	.id = SM8350_SLAVE_CX_RDPM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_dcc_cfg = {
	.name = "qhs_dcc_cfg",
	.id = SM8350_SLAVE_DCC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_display_cfg = {
	.name = "qhs_display_cfg",
	.id = SM8350_SLAVE_DISPLAY_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.id = SM8350_SLAVE_GFX3D_CFG,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qhs_hwkm = {
	.name = "qhs_hwkm",
	.id = SM8350_SLAVE_HWKM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SM8350_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ipa = {
	.name = "qhs_ipa",
	.id = SM8350_SLAVE_IPA_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ipc_router = {
	.name = "qhs_ipc_router",
	.id = SM8350_SLAVE_IPC_ROUTER_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_lpass_cfg = {
	.name = "qhs_lpass_cfg",
	.id = SM8350_SLAVE_LPASS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8350_MASTER_CNOC_LPASS_AG_NOC },
};

static struct qcom_icc_node qhs_mss_cfg = {
	.name = "qhs_mss_cfg",
	.id = SM8350_SLAVE_CNOC_MSS,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_mx_rdpm = {
	.name = "qhs_mx_rdpm",
	.id = SM8350_SLAVE_MX_RDPM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie0_cfg = {
	.name = "qhs_pcie0_cfg",
	.id = SM8350_SLAVE_PCIE_0_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie1_cfg = {
	.name = "qhs_pcie1_cfg",
	.id = SM8350_SLAVE_PCIE_1_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pdm = {
	.name = "qhs_pdm",
	.id = SM8350_SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = SM8350_SLAVE_PIMEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pka_wrapper_cfg = {
	.name = "qhs_pka_wrapper_cfg",
	.id = SM8350_SLAVE_PKA_WRAPPER_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pmu_wrapper_cfg = {
	.name = "qhs_pmu_wrapper_cfg",
	.id = SM8350_SLAVE_PMU_WRAPPER_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SM8350_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qspi = {
	.name = "qhs_qspi",
	.id = SM8350_SLAVE_QSPI_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qup0 = {
	.name = "qhs_qup0",
	.id = SM8350_SLAVE_QUP_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qup1 = {
	.name = "qhs_qup1",
	.id = SM8350_SLAVE_QUP_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qup2 = {
	.name = "qhs_qup2",
	.id = SM8350_SLAVE_QUP_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = SM8350_SLAVE_SDCC_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_sdc4 = {
	.name = "qhs_sdc4",
	.id = SM8350_SLAVE_SDCC_4,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_security = {
	.name = "qhs_security",
	.id = SM8350_SLAVE_SECURITY,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_spss_cfg = {
	.name = "qhs_spss_cfg",
	.id = SM8350_SLAVE_SPSS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SM8350_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_tlmm = {
	.name = "qhs_tlmm",
	.id = SM8350_SLAVE_TLMM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ufs_card_cfg = {
	.name = "qhs_ufs_card_cfg",
	.id = SM8350_SLAVE_UFS_CARD_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.id = SM8350_SLAVE_UFS_MEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb3_0 = {
	.name = "qhs_usb3_0",
	.id = SM8350_SLAVE_USB3_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb3_1 = {
	.name = "qhs_usb3_1",
	.id = SM8350_SLAVE_USB3_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = SM8350_SLAVE_VENUS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.id = SM8350_SLAVE_VSENSE_CTRL_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_a1_noc_cfg = {
	.name = "qns_a1_noc_cfg",
	.id = SM8350_SLAVE_A1NOC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_a2_noc_cfg = {
	.name = "qns_a2_noc_cfg",
	.id = SM8350_SLAVE_A2NOC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_ddrss_cfg = {
	.name = "qns_ddrss_cfg",
	.id = SM8350_SLAVE_DDRSS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_mnoc_cfg = {
	.name = "qns_mnoc_cfg",
	.id = SM8350_SLAVE_CNOC_MNOC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_snoc_cfg = {
	.name = "qns_snoc_cfg",
	.id = SM8350_SLAVE_SNOC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qxs_boot_imem = {
	.name = "qxs_boot_imem",
	.id = SM8350_SLAVE_BOOT_IMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qxs_imem = {
	.name = "qxs_imem",
	.id = SM8350_SLAVE_IMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qxs_pimem = {
	.name = "qxs_pimem",
	.id = SM8350_SLAVE_PIMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node srvc_cnoc = {
	.name = "srvc_cnoc",
	.id = SM8350_SLAVE_SERVICE_CNOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node xs_pcie_0 = {
	.name = "xs_pcie_0",
	.id = SM8350_SLAVE_PCIE_0,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node xs_pcie_1 = {
	.name = "xs_pcie_1",
	.id = SM8350_SLAVE_PCIE_1,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SM8350_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SM8350_SLAVE_TCU,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qhs_llcc = {
	.name = "qhs_llcc",
	.id = SM8350_SLAVE_LLCC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_gemnoc = {
	.name = "qns_gemnoc",
	.id = SM8350_SLAVE_GEM_NOC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_mdsp_ms_mpu_cfg = {
	.name = "qhs_mdsp_ms_mpu_cfg",
	.id = SM8350_SLAVE_MSS_PROC_MS_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_modem_ms_mpu_cfg = {
	.name = "qhs_modem_ms_mpu_cfg",
	.id = SM8350_SLAVE_MCDMA_MS_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_gem_noc_cnoc = {
	.name = "qns_gem_noc_cnoc",
	.id = SM8350_SLAVE_GEM_NOC_CNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8350_MASTER_GEM_NOC_CNOC },
};

static struct qcom_icc_node qns_llcc = {
	.name = "qns_llcc",
	.id = SM8350_SLAVE_LLCC,
	.channels = 4,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8350_MASTER_LLCC },
};

static struct qcom_icc_node qns_pcie = {
	.name = "qns_pcie",
	.id = SM8350_SLAVE_MEM_NOC_PCIE_SNOC,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node srvc_even_gemnoc = {
	.name = "srvc_even_gemnoc",
	.id = SM8350_SLAVE_SERVICE_GEM_NOC_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_odd_gemnoc = {
	.name = "srvc_odd_gemnoc",
	.id = SM8350_SLAVE_SERVICE_GEM_NOC_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_sys_gemnoc = {
	.name = "srvc_sys_gemnoc",
	.id = SM8350_SLAVE_SERVICE_GEM_NOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_lpass_core = {
	.name = "qhs_lpass_core",
	.id = SM8350_SLAVE_LPASS_CORE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_lpass_lpi = {
	.name = "qhs_lpass_lpi",
	.id = SM8350_SLAVE_LPASS_LPI_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_lpass_mpu = {
	.name = "qhs_lpass_mpu",
	.id = SM8350_SLAVE_LPASS_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_lpass_top = {
	.name = "qhs_lpass_top",
	.id = SM8350_SLAVE_LPASS_TOP_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_niu_aml_noc = {
	.name = "srvc_niu_aml_noc",
	.id = SM8350_SLAVE_SERVICES_LPASS_AML_NOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_niu_lpass_agnoc = {
	.name = "srvc_niu_lpass_agnoc",
	.id = SM8350_SLAVE_SERVICE_LPASS_AG_NOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.id = SM8350_SLAVE_EBI1,
	.channels = 4,
	.buswidth = 4,
};

static struct qcom_icc_node qns_mem_noc_hf = {
	.name = "qns_mem_noc_hf",
	.id = SM8350_SLAVE_MNOC_HF_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_MASTER_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qns_mem_noc_sf = {
	.name = "qns_mem_noc_sf",
	.id = SM8350_SLAVE_MNOC_SF_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_MASTER_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node srvc_mnoc = {
	.name = "srvc_mnoc",
	.id = SM8350_SLAVE_SERVICE_MNOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_nsp_gemnoc = {
	.name = "qns_nsp_gemnoc",
	.id = SM8350_SLAVE_CDSP_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_MASTER_COMPUTE_NOC },
};

static struct qcom_icc_node service_nsp_noc = {
	.name = "service_nsp_noc",
	.id = SM8350_SLAVE_SERVICE_NSP_NOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_gemnoc_gc = {
	.name = "qns_gemnoc_gc",
	.id = SM8350_SLAVE_SNOC_GEM_NOC_GC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8350_MASTER_SNOC_GC_MEM_NOC },
};

static struct qcom_icc_node qns_gemnoc_sf = {
	.name = "qns_gemnoc_sf",
	.id = SM8350_SLAVE_SNOC_GEM_NOC_SF,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8350_MASTER_SNOC_SF_MEM_NOC },
};

static struct qcom_icc_node srvc_snoc = {
	.name = "srvc_snoc",
	.id = SM8350_SLAVE_SERVICE_SNOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_llcc_disp = {
	.name = "qns_llcc_disp",
	.id = SM8350_SLAVE_LLCC_DISP,
	.channels = 4,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8350_MASTER_LLCC_DISP },
};

static struct qcom_icc_node ebi_disp = {
	.name = "ebi_disp",
	.id = SM8350_SLAVE_EBI1_DISP,
	.channels = 4,
	.buswidth = 4,
};

static struct qcom_icc_node qns_mem_noc_hf_disp = {
	.name = "qns_mem_noc_hf_disp",
	.id = SM8350_SLAVE_MNOC_HF_MEM_NOC_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_MASTER_MNOC_HF_MEM_NOC_DISP },
};

static struct qcom_icc_node qns_mem_noc_sf_disp = {
	.name = "qns_mem_noc_sf_disp",
	.id = SM8350_SLAVE_MNOC_SF_MEM_NOC_DISP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8350_MASTER_MNOC_SF_MEM_NOC_DISP },
};

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
	.enable_mask = BIT(3),
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.keepalive = true,
	.num_nodes = 2,
	.nodes = { &qnm_gemnoc_cnoc, &qnm_gemnoc_pcie },
};

static struct qcom_icc_bcm bcm_cn1 = {
	.name = "CN1",
	.keepalive = false,
	.num_nodes = 47,
	.nodes = { &xm_qdss_dap,
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
		   &qns_a1_noc_cfg,
		   &qns_a2_noc_cfg,
		   &qns_ddrss_cfg,
		   &qns_mnoc_cfg,
		   &qns_snoc_cfg,
		   &srvc_cnoc
	},
};

static struct qcom_icc_bcm bcm_cn2 = {
	.name = "CN2",
	.keepalive = false,
	.num_nodes = 5,
	.nodes = { &qhs_lpass_cfg, &qhs_pdm, &qhs_qspi, &qhs_sdc2, &qhs_sdc4 },
};

static struct qcom_icc_bcm bcm_co0 = {
	.name = "CO0",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qns_nsp_gemnoc },
};

static struct qcom_icc_bcm bcm_co3 = {
	.name = "CO3",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qxm_nsp },
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
	.keepalive = false,
	.num_nodes = 3,
	.nodes = { &qnm_camnoc_hf, &qxm_mdp0, &qxm_mdp1 },
};

static struct qcom_icc_bcm bcm_mm4 = {
	.name = "MM4",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_bcm bcm_mm5 = {
	.name = "MM5",
	.keepalive = false,
	.num_nodes = 6,
	.nodes = { &qnm_camnoc_icp,
		   &qnm_camnoc_sf,
		   &qnm_video0,
		   &qnm_video1,
		   &qnm_video_cvp,
		   &qxm_rot
	},
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_sh2 = {
	.name = "SH2",
	.keepalive = false,
	.num_nodes = 2,
	.nodes = { &alm_gpu_tcu, &alm_sys_tcu },
};

static struct qcom_icc_bcm bcm_sh3 = {
	.name = "SH3",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qnm_cmpnoc },
};

static struct qcom_icc_bcm bcm_sh4 = {
	.name = "SH4",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &chm_apps },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qns_gemnoc_gc },
};

static struct qcom_icc_bcm bcm_sn3 = {
	.name = "SN3",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qxs_pimem },
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &xs_qdss_stm },
};

static struct qcom_icc_bcm bcm_sn5 = {
	.name = "SN5",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &xm_pcie3_0 },
};

static struct qcom_icc_bcm bcm_sn6 = {
	.name = "SN6",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &xm_pcie3_1 },
};

static struct qcom_icc_bcm bcm_sn7 = {
	.name = "SN7",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qnm_aggre1_noc },
};

static struct qcom_icc_bcm bcm_sn8 = {
	.name = "SN8",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qnm_aggre2_noc },
};

static struct qcom_icc_bcm bcm_sn14 = {
	.name = "SN14",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qns_pcie_mem_noc },
};

static struct qcom_icc_bcm bcm_acv_disp = {
	.name = "ACV",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &ebi_disp },
};

static struct qcom_icc_bcm bcm_mc0_disp = {
	.name = "MC0",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &ebi_disp },
};

static struct qcom_icc_bcm bcm_mm0_disp = {
	.name = "MM0",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qns_mem_noc_hf_disp },
};

static struct qcom_icc_bcm bcm_mm1_disp = {
	.name = "MM1",
	.keepalive = false,
	.num_nodes = 2,
	.nodes = { &qxm_mdp0_disp, &qxm_mdp1_disp },
};

static struct qcom_icc_bcm bcm_mm4_disp = {
	.name = "MM4",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qns_mem_noc_sf_disp },
};

static struct qcom_icc_bcm bcm_mm5_disp = {
	.name = "MM5",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qxm_rot_disp },
};

static struct qcom_icc_bcm bcm_sh0_disp = {
	.name = "SH0",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qns_llcc_disp },
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
	[MASTER_USB3_1] = &xm_usb3_1,
	[SLAVE_A1NOC_SNOC] = &qns_a1noc_snoc,
	[SLAVE_SERVICE_A1NOC] = &srvc_aggre1_noc,
};

static const struct qcom_icc_desc sm8350_aggre1_noc = {
	.nodes = aggre1_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre1_noc_nodes),
	.bcms = aggre1_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_noc_bcms),
};

static struct qcom_icc_bcm * const aggre2_noc_bcms[] = {
	&bcm_ce0,
	&bcm_sn5,
	&bcm_sn6,
	&bcm_sn14,
};

static struct qcom_icc_node * const aggre2_noc_nodes[] = {
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_QUP_2] = &qhm_qup2,
	[MASTER_A2NOC_CFG] = &qnm_a2noc_cfg,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_PCIE_0] = &xm_pcie3_0,
	[MASTER_PCIE_1] = &xm_pcie3_1,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_UFS_CARD] = &xm_ufs_card,
	[SLAVE_A2NOC_SNOC] = &qns_a2noc_snoc,
	[SLAVE_ANOC_PCIE_GEM_NOC] = &qns_pcie_mem_noc,
	[SLAVE_SERVICE_A2NOC] = &srvc_aggre2_noc,
};

static const struct qcom_icc_desc sm8350_aggre2_noc = {
	.nodes = aggre2_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre2_noc_nodes),
	.bcms = aggre2_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_noc_bcms),
};

static struct qcom_icc_bcm * const config_noc_bcms[] = {
	&bcm_cn0,
	&bcm_cn1,
	&bcm_cn2,
	&bcm_sn3,
	&bcm_sn4,
};

static struct qcom_icc_node * const config_noc_nodes[] = {
	[MASTER_GEM_NOC_CNOC] = &qnm_gemnoc_cnoc,
	[MASTER_GEM_NOC_PCIE_SNOC] = &qnm_gemnoc_pcie,
	[MASTER_QDSS_DAP] = &xm_qdss_dap,
	[SLAVE_AHB2PHY_SOUTH] = &qhs_ahb2phy0,
	[SLAVE_AHB2PHY_NORTH] = &qhs_ahb2phy1,
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
	[SLAVE_CNOC_MSS] = &qhs_mss_cfg,
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
	[SLAVE_A1NOC_CFG] = &qns_a1_noc_cfg,
	[SLAVE_A2NOC_CFG] = &qns_a2_noc_cfg,
	[SLAVE_DDRSS_CFG] = &qns_ddrss_cfg,
	[SLAVE_CNOC_MNOC_CFG] = &qns_mnoc_cfg,
	[SLAVE_SNOC_CFG] = &qns_snoc_cfg,
	[SLAVE_BOOT_IMEM] = &qxs_boot_imem,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SERVICE_CNOC] = &srvc_cnoc,
	[SLAVE_PCIE_0] = &xs_pcie_0,
	[SLAVE_PCIE_1] = &xs_pcie_1,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sm8350_config_noc = {
	.nodes = config_noc_nodes,
	.num_nodes = ARRAY_SIZE(config_noc_nodes),
	.bcms = config_noc_bcms,
	.num_bcms = ARRAY_SIZE(config_noc_bcms),
};

static struct qcom_icc_bcm * const dc_noc_bcms[] = {
};

static struct qcom_icc_node * const dc_noc_nodes[] = {
	[MASTER_CNOC_DC_NOC] = &qnm_cnoc_dc_noc,
	[SLAVE_LLCC_CFG] = &qhs_llcc,
	[SLAVE_GEM_NOC_CFG] = &qns_gemnoc,
};

static const struct qcom_icc_desc sm8350_dc_noc = {
	.nodes = dc_noc_nodes,
	.num_nodes = ARRAY_SIZE(dc_noc_nodes),
	.bcms = dc_noc_bcms,
	.num_bcms = ARRAY_SIZE(dc_noc_bcms),
};

static struct qcom_icc_bcm * const gem_noc_bcms[] = {
	&bcm_sh0,
	&bcm_sh2,
	&bcm_sh3,
	&bcm_sh4,
	&bcm_sh0_disp,
};

static struct qcom_icc_node * const gem_noc_nodes[] = {
	[MASTER_GPU_TCU] = &alm_gpu_tcu,
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_APPSS_PROC] = &chm_apps,
	[MASTER_COMPUTE_NOC] = &qnm_cmpnoc,
	[MASTER_GEM_NOC_CFG] = &qnm_gemnoc_cfg,
	[MASTER_GFX3D] = &qnm_gpu,
	[MASTER_MNOC_HF_MEM_NOC] = &qnm_mnoc_hf,
	[MASTER_MNOC_SF_MEM_NOC] = &qnm_mnoc_sf,
	[MASTER_ANOC_PCIE_GEM_NOC] = &qnm_pcie,
	[MASTER_SNOC_GC_MEM_NOC] = &qnm_snoc_gc,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[SLAVE_MSS_PROC_MS_MPU_CFG] = &qhs_mdsp_ms_mpu_cfg,
	[SLAVE_MCDMA_MS_MPU_CFG] = &qhs_modem_ms_mpu_cfg,
	[SLAVE_GEM_NOC_CNOC] = &qns_gem_noc_cnoc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_MEM_NOC_PCIE_SNOC] = &qns_pcie,
	[SLAVE_SERVICE_GEM_NOC_1] = &srvc_even_gemnoc,
	[SLAVE_SERVICE_GEM_NOC_2] = &srvc_odd_gemnoc,
	[SLAVE_SERVICE_GEM_NOC] = &srvc_sys_gemnoc,
	[MASTER_MNOC_HF_MEM_NOC_DISP] = &qnm_mnoc_hf_disp,
	[MASTER_MNOC_SF_MEM_NOC_DISP] = &qnm_mnoc_sf_disp,
	[SLAVE_LLCC_DISP] = &qns_llcc_disp,
};

static const struct qcom_icc_desc sm8350_gem_noc = {
	.nodes = gem_noc_nodes,
	.num_nodes = ARRAY_SIZE(gem_noc_nodes),
	.bcms = gem_noc_bcms,
	.num_bcms = ARRAY_SIZE(gem_noc_bcms),
};

static struct qcom_icc_bcm * const lpass_ag_noc_bcms[] = {
};

static struct qcom_icc_node * const lpass_ag_noc_nodes[] = {
	[MASTER_CNOC_LPASS_AG_NOC] = &qhm_config_noc,
	[SLAVE_LPASS_CORE_CFG] = &qhs_lpass_core,
	[SLAVE_LPASS_LPI_CFG] = &qhs_lpass_lpi,
	[SLAVE_LPASS_MPU_CFG] = &qhs_lpass_mpu,
	[SLAVE_LPASS_TOP_CFG] = &qhs_lpass_top,
	[SLAVE_SERVICES_LPASS_AML_NOC] = &srvc_niu_aml_noc,
	[SLAVE_SERVICE_LPASS_AG_NOC] = &srvc_niu_lpass_agnoc,
};

static const struct qcom_icc_desc sm8350_lpass_ag_noc = {
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

static const struct qcom_icc_desc sm8350_mc_virt = {
	.nodes = mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm * const mmss_noc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
	&bcm_mm4,
	&bcm_mm5,
	&bcm_mm0_disp,
	&bcm_mm1_disp,
	&bcm_mm4_disp,
	&bcm_mm5_disp,
};

static struct qcom_icc_node * const mmss_noc_nodes[] = {
	[MASTER_CAMNOC_HF] = &qnm_camnoc_hf,
	[MASTER_CAMNOC_ICP] = &qnm_camnoc_icp,
	[MASTER_CAMNOC_SF] = &qnm_camnoc_sf,
	[MASTER_CNOC_MNOC_CFG] = &qnm_mnoc_cfg,
	[MASTER_VIDEO_P0] = &qnm_video0,
	[MASTER_VIDEO_P1] = &qnm_video1,
	[MASTER_VIDEO_PROC] = &qnm_video_cvp,
	[MASTER_MDP0] = &qxm_mdp0,
	[MASTER_MDP1] = &qxm_mdp1,
	[MASTER_ROTATOR] = &qxm_rot,
	[SLAVE_MNOC_HF_MEM_NOC] = &qns_mem_noc_hf,
	[SLAVE_MNOC_SF_MEM_NOC] = &qns_mem_noc_sf,
	[SLAVE_SERVICE_MNOC] = &srvc_mnoc,
	[MASTER_MDP0_DISP] = &qxm_mdp0_disp,
	[MASTER_MDP1_DISP] = &qxm_mdp1_disp,
	[MASTER_ROTATOR_DISP] = &qxm_rot_disp,
	[SLAVE_MNOC_HF_MEM_NOC_DISP] = &qns_mem_noc_hf_disp,
	[SLAVE_MNOC_SF_MEM_NOC_DISP] = &qns_mem_noc_sf_disp,
};

static const struct qcom_icc_desc sm8350_mmss_noc = {
	.nodes = mmss_noc_nodes,
	.num_nodes = ARRAY_SIZE(mmss_noc_nodes),
	.bcms = mmss_noc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_noc_bcms),
};

static struct qcom_icc_bcm * const nsp_noc_bcms[] = {
	&bcm_co0,
	&bcm_co3,
};

static struct qcom_icc_node * const nsp_noc_nodes[] = {
	[MASTER_CDSP_NOC_CFG] = &qhm_nsp_noc_config,
	[MASTER_CDSP_PROC] = &qxm_nsp,
	[SLAVE_CDSP_MEM_NOC] = &qns_nsp_gemnoc,
	[SLAVE_SERVICE_NSP_NOC] = &service_nsp_noc,
};

static const struct qcom_icc_desc sm8350_compute_noc = {
	.nodes = nsp_noc_nodes,
	.num_nodes = ARRAY_SIZE(nsp_noc_nodes),
	.bcms = nsp_noc_bcms,
	.num_bcms = ARRAY_SIZE(nsp_noc_bcms),
};

static struct qcom_icc_bcm * const system_noc_bcms[] = {
	&bcm_sn0,
	&bcm_sn2,
	&bcm_sn7,
	&bcm_sn8,
};

static struct qcom_icc_node * const system_noc_nodes[] = {
	[MASTER_A1NOC_SNOC] = &qnm_aggre1_noc,
	[MASTER_A2NOC_SNOC] = &qnm_aggre2_noc,
	[MASTER_SNOC_CFG] = &qnm_snoc_cfg,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_GIC] = &xm_gic,
	[SLAVE_SNOC_GEM_NOC_GC] = &qns_gemnoc_gc,
	[SLAVE_SNOC_GEM_NOC_SF] = &qns_gemnoc_sf,
	[SLAVE_SERVICE_SNOC] = &srvc_snoc,
};

static const struct qcom_icc_desc sm8350_system_noc = {
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,sm8350-aggre1-noc", .data = &sm8350_aggre1_noc},
	{ .compatible = "qcom,sm8350-aggre2-noc", .data = &sm8350_aggre2_noc},
	{ .compatible = "qcom,sm8350-config-noc", .data = &sm8350_config_noc},
	{ .compatible = "qcom,sm8350-dc-noc", .data = &sm8350_dc_noc},
	{ .compatible = "qcom,sm8350-gem-noc", .data = &sm8350_gem_noc},
	{ .compatible = "qcom,sm8350-lpass-ag-noc", .data = &sm8350_lpass_ag_noc},
	{ .compatible = "qcom,sm8350-mc-virt", .data = &sm8350_mc_virt},
	{ .compatible = "qcom,sm8350-mmss-noc", .data = &sm8350_mmss_noc},
	{ .compatible = "qcom,sm8350-compute-noc", .data = &sm8350_compute_noc},
	{ .compatible = "qcom,sm8350-system-noc", .data = &sm8350_system_noc},
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove_new = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-sm8350",
		.of_match_table = qnoc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(qnoc_driver);

MODULE_DESCRIPTION("SM8350 NoC driver");
MODULE_LICENSE("GPL v2");
