// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

#include <dt-bindings/interconnect/qcom,sdm845.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"
#include "sdm845.h"

static struct qcom_icc_analde qhm_a1analc_cfg = {
	.name = "qhm_a1analc_cfg",
	.id = SDM845_MASTER_A1ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDM845_SLAVE_SERVICE_A1ANALC },
};

static struct qcom_icc_analde qhm_qup1 = {
	.name = "qhm_qup1",
	.id = SDM845_MASTER_BLSP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDM845_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qhm_tsif = {
	.name = "qhm_tsif",
	.id = SDM845_MASTER_TSIF,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDM845_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_sdc2 = {
	.name = "xm_sdc2",
	.id = SDM845_MASTER_SDCC_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDM845_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_sdc4 = {
	.name = "xm_sdc4",
	.id = SDM845_MASTER_SDCC_4,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDM845_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_ufs_card = {
	.name = "xm_ufs_card",
	.id = SDM845_MASTER_UFS_CARD,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDM845_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.id = SDM845_MASTER_UFS_MEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDM845_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_pcie_0 = {
	.name = "xm_pcie_0",
	.id = SDM845_MASTER_PCIE_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDM845_SLAVE_AANALC_PCIE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qhm_a2analc_cfg = {
	.name = "qhm_a2analc_cfg",
	.id = SDM845_MASTER_A2ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDM845_SLAVE_SERVICE_A2ANALC },
};

static struct qcom_icc_analde qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = SDM845_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDM845_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qup2 = {
	.name = "qhm_qup2",
	.id = SDM845_MASTER_BLSP_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDM845_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qnm_canalc = {
	.name = "qnm_canalc",
	.id = SDM845_MASTER_CANALC_A2ANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDM845_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qxm_crypto = {
	.name = "qxm_crypto",
	.id = SDM845_MASTER_CRYPTO,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDM845_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qxm_ipa = {
	.name = "qxm_ipa",
	.id = SDM845_MASTER_IPA,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDM845_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde xm_pcie3_1 = {
	.name = "xm_pcie3_1",
	.id = SDM845_MASTER_PCIE_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDM845_SLAVE_AANALC_PCIE_SANALC },
};

static struct qcom_icc_analde xm_qdss_etr = {
	.name = "xm_qdss_etr",
	.id = SDM845_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDM845_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde xm_usb3_0 = {
	.name = "xm_usb3_0",
	.id = SDM845_MASTER_USB3_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDM845_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde xm_usb3_1 = {
	.name = "xm_usb3_1",
	.id = SDM845_MASTER_USB3_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDM845_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qxm_camanalc_hf0_uncomp = {
	.name = "qxm_camanalc_hf0_uncomp",
	.id = SDM845_MASTER_CAMANALC_HF0_UNCOMP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SDM845_SLAVE_CAMANALC_UNCOMP },
};

static struct qcom_icc_analde qxm_camanalc_hf1_uncomp = {
	.name = "qxm_camanalc_hf1_uncomp",
	.id = SDM845_MASTER_CAMANALC_HF1_UNCOMP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SDM845_SLAVE_CAMANALC_UNCOMP },
};

static struct qcom_icc_analde qxm_camanalc_sf_uncomp = {
	.name = "qxm_camanalc_sf_uncomp",
	.id = SDM845_MASTER_CAMANALC_SF_UNCOMP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SDM845_SLAVE_CAMANALC_UNCOMP },
};

static struct qcom_icc_analde qhm_spdm = {
	.name = "qhm_spdm",
	.id = SDM845_MASTER_SPDM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDM845_SLAVE_CANALC_A2ANALC },
};

static struct qcom_icc_analde qhm_tic = {
	.name = "qhm_tic",
	.id = SDM845_MASTER_TIC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 43,
	.links = { SDM845_SLAVE_A1ANALC_CFG,
		   SDM845_SLAVE_A2ANALC_CFG,
		   SDM845_SLAVE_AOP,
		   SDM845_SLAVE_AOSS,
		   SDM845_SLAVE_CAMERA_CFG,
		   SDM845_SLAVE_CLK_CTL,
		   SDM845_SLAVE_CDSP_CFG,
		   SDM845_SLAVE_RBCPR_CX_CFG,
		   SDM845_SLAVE_CRYPTO_0_CFG,
		   SDM845_SLAVE_DCC_CFG,
		   SDM845_SLAVE_CANALC_DDRSS,
		   SDM845_SLAVE_DISPLAY_CFG,
		   SDM845_SLAVE_GLM,
		   SDM845_SLAVE_GFX3D_CFG,
		   SDM845_SLAVE_IMEM_CFG,
		   SDM845_SLAVE_IPA_CFG,
		   SDM845_SLAVE_CANALC_MANALC_CFG,
		   SDM845_SLAVE_PCIE_0_CFG,
		   SDM845_SLAVE_PCIE_1_CFG,
		   SDM845_SLAVE_PDM,
		   SDM845_SLAVE_SOUTH_PHY_CFG,
		   SDM845_SLAVE_PIMEM_CFG,
		   SDM845_SLAVE_PRNG,
		   SDM845_SLAVE_QDSS_CFG,
		   SDM845_SLAVE_BLSP_2,
		   SDM845_SLAVE_BLSP_1,
		   SDM845_SLAVE_SDCC_2,
		   SDM845_SLAVE_SDCC_4,
		   SDM845_SLAVE_SANALC_CFG,
		   SDM845_SLAVE_SPDM_WRAPPER,
		   SDM845_SLAVE_SPSS_CFG,
		   SDM845_SLAVE_TCSR,
		   SDM845_SLAVE_TLMM_ANALRTH,
		   SDM845_SLAVE_TLMM_SOUTH,
		   SDM845_SLAVE_TSIF,
		   SDM845_SLAVE_UFS_CARD_CFG,
		   SDM845_SLAVE_UFS_MEM_CFG,
		   SDM845_SLAVE_USB3_0,
		   SDM845_SLAVE_USB3_1,
		   SDM845_SLAVE_VENUS_CFG,
		   SDM845_SLAVE_VSENSE_CTRL_CFG,
		   SDM845_SLAVE_CANALC_A2ANALC,
		   SDM845_SLAVE_SERVICE_CANALC
	},
};

static struct qcom_icc_analde qnm_sanalc = {
	.name = "qnm_sanalc",
	.id = SDM845_MASTER_SANALC_CANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 42,
	.links = { SDM845_SLAVE_A1ANALC_CFG,
		   SDM845_SLAVE_A2ANALC_CFG,
		   SDM845_SLAVE_AOP,
		   SDM845_SLAVE_AOSS,
		   SDM845_SLAVE_CAMERA_CFG,
		   SDM845_SLAVE_CLK_CTL,
		   SDM845_SLAVE_CDSP_CFG,
		   SDM845_SLAVE_RBCPR_CX_CFG,
		   SDM845_SLAVE_CRYPTO_0_CFG,
		   SDM845_SLAVE_DCC_CFG,
		   SDM845_SLAVE_CANALC_DDRSS,
		   SDM845_SLAVE_DISPLAY_CFG,
		   SDM845_SLAVE_GLM,
		   SDM845_SLAVE_GFX3D_CFG,
		   SDM845_SLAVE_IMEM_CFG,
		   SDM845_SLAVE_IPA_CFG,
		   SDM845_SLAVE_CANALC_MANALC_CFG,
		   SDM845_SLAVE_PCIE_0_CFG,
		   SDM845_SLAVE_PCIE_1_CFG,
		   SDM845_SLAVE_PDM,
		   SDM845_SLAVE_SOUTH_PHY_CFG,
		   SDM845_SLAVE_PIMEM_CFG,
		   SDM845_SLAVE_PRNG,
		   SDM845_SLAVE_QDSS_CFG,
		   SDM845_SLAVE_BLSP_2,
		   SDM845_SLAVE_BLSP_1,
		   SDM845_SLAVE_SDCC_2,
		   SDM845_SLAVE_SDCC_4,
		   SDM845_SLAVE_SANALC_CFG,
		   SDM845_SLAVE_SPDM_WRAPPER,
		   SDM845_SLAVE_SPSS_CFG,
		   SDM845_SLAVE_TCSR,
		   SDM845_SLAVE_TLMM_ANALRTH,
		   SDM845_SLAVE_TLMM_SOUTH,
		   SDM845_SLAVE_TSIF,
		   SDM845_SLAVE_UFS_CARD_CFG,
		   SDM845_SLAVE_UFS_MEM_CFG,
		   SDM845_SLAVE_USB3_0,
		   SDM845_SLAVE_USB3_1,
		   SDM845_SLAVE_VENUS_CFG,
		   SDM845_SLAVE_VSENSE_CTRL_CFG,
		   SDM845_SLAVE_SERVICE_CANALC
	},
};

static struct qcom_icc_analde xm_qdss_dap = {
	.name = "xm_qdss_dap",
	.id = SDM845_MASTER_QDSS_DAP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 43,
	.links = { SDM845_SLAVE_A1ANALC_CFG,
		   SDM845_SLAVE_A2ANALC_CFG,
		   SDM845_SLAVE_AOP,
		   SDM845_SLAVE_AOSS,
		   SDM845_SLAVE_CAMERA_CFG,
		   SDM845_SLAVE_CLK_CTL,
		   SDM845_SLAVE_CDSP_CFG,
		   SDM845_SLAVE_RBCPR_CX_CFG,
		   SDM845_SLAVE_CRYPTO_0_CFG,
		   SDM845_SLAVE_DCC_CFG,
		   SDM845_SLAVE_CANALC_DDRSS,
		   SDM845_SLAVE_DISPLAY_CFG,
		   SDM845_SLAVE_GLM,
		   SDM845_SLAVE_GFX3D_CFG,
		   SDM845_SLAVE_IMEM_CFG,
		   SDM845_SLAVE_IPA_CFG,
		   SDM845_SLAVE_CANALC_MANALC_CFG,
		   SDM845_SLAVE_PCIE_0_CFG,
		   SDM845_SLAVE_PCIE_1_CFG,
		   SDM845_SLAVE_PDM,
		   SDM845_SLAVE_SOUTH_PHY_CFG,
		   SDM845_SLAVE_PIMEM_CFG,
		   SDM845_SLAVE_PRNG,
		   SDM845_SLAVE_QDSS_CFG,
		   SDM845_SLAVE_BLSP_2,
		   SDM845_SLAVE_BLSP_1,
		   SDM845_SLAVE_SDCC_2,
		   SDM845_SLAVE_SDCC_4,
		   SDM845_SLAVE_SANALC_CFG,
		   SDM845_SLAVE_SPDM_WRAPPER,
		   SDM845_SLAVE_SPSS_CFG,
		   SDM845_SLAVE_TCSR,
		   SDM845_SLAVE_TLMM_ANALRTH,
		   SDM845_SLAVE_TLMM_SOUTH,
		   SDM845_SLAVE_TSIF,
		   SDM845_SLAVE_UFS_CARD_CFG,
		   SDM845_SLAVE_UFS_MEM_CFG,
		   SDM845_SLAVE_USB3_0,
		   SDM845_SLAVE_USB3_1,
		   SDM845_SLAVE_VENUS_CFG,
		   SDM845_SLAVE_VSENSE_CTRL_CFG,
		   SDM845_SLAVE_CANALC_A2ANALC,
		   SDM845_SLAVE_SERVICE_CANALC
	},
};

static struct qcom_icc_analde qhm_canalc = {
	.name = "qhm_canalc",
	.id = SDM845_MASTER_CANALC_DC_ANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.links = { SDM845_SLAVE_LLCC_CFG,
		   SDM845_SLAVE_MEM_ANALC_CFG
	},
};

static struct qcom_icc_analde acm_l3 = {
	.name = "acm_l3",
	.id = SDM845_MASTER_APPSS_PROC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { SDM845_SLAVE_GANALC_SANALC,
		   SDM845_SLAVE_GANALC_MEM_ANALC,
		   SDM845_SLAVE_SERVICE_GANALC
	},
};

static struct qcom_icc_analde pm_ganalc_cfg = {
	.name = "pm_ganalc_cfg",
	.id = SDM845_MASTER_GANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDM845_SLAVE_SERVICE_GANALC },
};

static struct qcom_icc_analde llcc_mc = {
	.name = "llcc_mc",
	.id = SDM845_MASTER_LLCC,
	.channels = 4,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDM845_SLAVE_EBI1 },
};

static struct qcom_icc_analde acm_tcu = {
	.name = "acm_tcu",
	.id = SDM845_MASTER_TCU_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 3,
	.links = { SDM845_SLAVE_MEM_ANALC_GANALC,
		   SDM845_SLAVE_LLCC,
		   SDM845_SLAVE_MEM_ANALC_SANALC
	},
};

static struct qcom_icc_analde qhm_memanalc_cfg = {
	.name = "qhm_memanalc_cfg",
	.id = SDM845_MASTER_MEM_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.links = { SDM845_SLAVE_MSS_PROC_MS_MPU_CFG,
		   SDM845_SLAVE_SERVICE_MEM_ANALC
	},
};

static struct qcom_icc_analde qnm_apps = {
	.name = "qnm_apps",
	.id = SDM845_MASTER_GANALC_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SDM845_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_manalc_hf = {
	.name = "qnm_manalc_hf",
	.id = SDM845_MASTER_MANALC_HF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SDM845_SLAVE_MEM_ANALC_GANALC,
		   SDM845_SLAVE_LLCC
	},
};

static struct qcom_icc_analde qnm_manalc_sf = {
	.name = "qnm_manalc_sf",
	.id = SDM845_MASTER_MANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 3,
	.links = { SDM845_SLAVE_MEM_ANALC_GANALC,
		   SDM845_SLAVE_LLCC,
		   SDM845_SLAVE_MEM_ANALC_SANALC
	},
};

static struct qcom_icc_analde qnm_sanalc_gc = {
	.name = "qnm_sanalc_gc",
	.id = SDM845_MASTER_SANALC_GC_MEM_ANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDM845_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_sanalc_sf = {
	.name = "qnm_sanalc_sf",
	.id = SDM845_MASTER_SANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 2,
	.links = { SDM845_SLAVE_MEM_ANALC_GANALC,
		   SDM845_SLAVE_LLCC
	},
};

static struct qcom_icc_analde qxm_gpu = {
	.name = "qxm_gpu",
	.id = SDM845_MASTER_GFX3D,
	.channels = 2,
	.buswidth = 32,
	.num_links = 3,
	.links = { SDM845_SLAVE_MEM_ANALC_GANALC,
		   SDM845_SLAVE_LLCC,
		   SDM845_SLAVE_MEM_ANALC_SANALC
	},
};

static struct qcom_icc_analde qhm_manalc_cfg = {
	.name = "qhm_manalc_cfg",
	.id = SDM845_MASTER_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDM845_SLAVE_SERVICE_MANALC },
};

static struct qcom_icc_analde qxm_camanalc_hf0 = {
	.name = "qxm_camanalc_hf0",
	.id = SDM845_MASTER_CAMANALC_HF0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SDM845_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_camanalc_hf1 = {
	.name = "qxm_camanalc_hf1",
	.id = SDM845_MASTER_CAMANALC_HF1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SDM845_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_camanalc_sf = {
	.name = "qxm_camanalc_sf",
	.id = SDM845_MASTER_CAMANALC_SF,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SDM845_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_mdp0 = {
	.name = "qxm_mdp0",
	.id = SDM845_MASTER_MDP0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SDM845_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_mdp1 = {
	.name = "qxm_mdp1",
	.id = SDM845_MASTER_MDP1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SDM845_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_rot = {
	.name = "qxm_rot",
	.id = SDM845_MASTER_ROTATOR,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SDM845_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_venus0 = {
	.name = "qxm_venus0",
	.id = SDM845_MASTER_VIDEO_P0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SDM845_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_venus1 = {
	.name = "qxm_venus1",
	.id = SDM845_MASTER_VIDEO_P1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SDM845_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_venus_arm9 = {
	.name = "qxm_venus_arm9",
	.id = SDM845_MASTER_VIDEO_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDM845_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qhm_sanalc_cfg = {
	.name = "qhm_sanalc_cfg",
	.id = SDM845_MASTER_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDM845_SLAVE_SERVICE_SANALC },
};

static struct qcom_icc_analde qnm_aggre1_analc = {
	.name = "qnm_aggre1_analc",
	.id = SDM845_MASTER_A1ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 6,
	.links = { SDM845_SLAVE_APPSS,
		   SDM845_SLAVE_SANALC_CANALC,
		   SDM845_SLAVE_SANALC_MEM_ANALC_SF,
		   SDM845_SLAVE_IMEM,
		   SDM845_SLAVE_PIMEM,
		   SDM845_SLAVE_QDSS_STM
	},
};

static struct qcom_icc_analde qnm_aggre2_analc = {
	.name = "qnm_aggre2_analc",
	.id = SDM845_MASTER_A2ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 9,
	.links = { SDM845_SLAVE_APPSS,
		   SDM845_SLAVE_SANALC_CANALC,
		   SDM845_SLAVE_SANALC_MEM_ANALC_SF,
		   SDM845_SLAVE_IMEM,
		   SDM845_SLAVE_PCIE_0,
		   SDM845_SLAVE_PCIE_1,
		   SDM845_SLAVE_PIMEM,
		   SDM845_SLAVE_QDSS_STM,
		   SDM845_SLAVE_TCU
	},
};

static struct qcom_icc_analde qnm_gladiator_sodv = {
	.name = "qnm_gladiator_sodv",
	.id = SDM845_MASTER_GANALC_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 8,
	.links = { SDM845_SLAVE_APPSS,
		   SDM845_SLAVE_SANALC_CANALC,
		   SDM845_SLAVE_IMEM,
		   SDM845_SLAVE_PCIE_0,
		   SDM845_SLAVE_PCIE_1,
		   SDM845_SLAVE_PIMEM,
		   SDM845_SLAVE_QDSS_STM,
		   SDM845_SLAVE_TCU
	},
};

static struct qcom_icc_analde qnm_memanalc = {
	.name = "qnm_memanalc",
	.id = SDM845_MASTER_MEM_ANALC_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 5,
	.links = { SDM845_SLAVE_APPSS,
		   SDM845_SLAVE_SANALC_CANALC,
		   SDM845_SLAVE_IMEM,
		   SDM845_SLAVE_PIMEM,
		   SDM845_SLAVE_QDSS_STM
	},
};

static struct qcom_icc_analde qnm_pcie_aanalc = {
	.name = "qnm_pcie_aanalc",
	.id = SDM845_MASTER_AANALC_PCIE_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 5,
	.links = { SDM845_SLAVE_APPSS,
		   SDM845_SLAVE_SANALC_CANALC,
		   SDM845_SLAVE_SANALC_MEM_ANALC_SF,
		   SDM845_SLAVE_IMEM,
		   SDM845_SLAVE_QDSS_STM
	},
};

static struct qcom_icc_analde qxm_pimem = {
	.name = "qxm_pimem",
	.id = SDM845_MASTER_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SDM845_SLAVE_SANALC_MEM_ANALC_GC,
		   SDM845_SLAVE_IMEM
	},
};

static struct qcom_icc_analde xm_gic = {
	.name = "xm_gic",
	.id = SDM845_MASTER_GIC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SDM845_SLAVE_SANALC_MEM_ANALC_GC,
		   SDM845_SLAVE_IMEM
	},
};

static struct qcom_icc_analde qns_a1analc_sanalc = {
	.name = "qns_a1analc_sanalc",
	.id = SDM845_SLAVE_A1ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SDM845_MASTER_A1ANALC_SANALC },
};

static struct qcom_icc_analde srvc_aggre1_analc = {
	.name = "srvc_aggre1_analc",
	.id = SDM845_SLAVE_SERVICE_A1ANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { 0 },
};

static struct qcom_icc_analde qns_pcie_a1analc_sanalc = {
	.name = "qns_pcie_a1analc_sanalc",
	.id = SDM845_SLAVE_AANALC_PCIE_A1ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SDM845_MASTER_AANALC_PCIE_SANALC },
};

static struct qcom_icc_analde qns_a2analc_sanalc = {
	.name = "qns_a2analc_sanalc",
	.id = SDM845_SLAVE_A2ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SDM845_MASTER_A2ANALC_SANALC },
};

static struct qcom_icc_analde qns_pcie_sanalc = {
	.name = "qns_pcie_sanalc",
	.id = SDM845_SLAVE_AANALC_PCIE_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SDM845_MASTER_AANALC_PCIE_SANALC },
};

static struct qcom_icc_analde srvc_aggre2_analc = {
	.name = "srvc_aggre2_analc",
	.id = SDM845_SLAVE_SERVICE_A2ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_camanalc_uncomp = {
	.name = "qns_camanalc_uncomp",
	.id = SDM845_SLAVE_CAMANALC_UNCOMP,
	.channels = 1,
	.buswidth = 32,
};

static struct qcom_icc_analde qhs_a1_analc_cfg = {
	.name = "qhs_a1_analc_cfg",
	.id = SDM845_SLAVE_A1ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDM845_MASTER_A1ANALC_CFG },
};

static struct qcom_icc_analde qhs_a2_analc_cfg = {
	.name = "qhs_a2_analc_cfg",
	.id = SDM845_SLAVE_A2ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDM845_MASTER_A2ANALC_CFG },
};

static struct qcom_icc_analde qhs_aop = {
	.name = "qhs_aop",
	.id = SDM845_SLAVE_AOP,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_aoss = {
	.name = "qhs_aoss",
	.id = SDM845_SLAVE_AOSS,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_camera_cfg = {
	.name = "qhs_camera_cfg",
	.id = SDM845_SLAVE_CAMERA_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SDM845_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_compute_dsp_cfg = {
	.name = "qhs_compute_dsp_cfg",
	.id = SDM845_SLAVE_CDSP_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = SDM845_SLAVE_RBCPR_CX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = SDM845_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_dcc_cfg = {
	.name = "qhs_dcc_cfg",
	.id = SDM845_SLAVE_DCC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDM845_MASTER_CANALC_DC_ANALC },
};

static struct qcom_icc_analde qhs_ddrss_cfg = {
	.name = "qhs_ddrss_cfg",
	.id = SDM845_SLAVE_CANALC_DDRSS,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_display_cfg = {
	.name = "qhs_display_cfg",
	.id = SDM845_SLAVE_DISPLAY_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_glm = {
	.name = "qhs_glm",
	.id = SDM845_SLAVE_GLM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.id = SDM845_SLAVE_GFX3D_CFG,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SDM845_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ipa = {
	.name = "qhs_ipa",
	.id = SDM845_SLAVE_IPA_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_manalc_cfg = {
	.name = "qhs_manalc_cfg",
	.id = SDM845_SLAVE_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDM845_MASTER_CANALC_MANALC_CFG },
};

static struct qcom_icc_analde qhs_pcie0_cfg = {
	.name = "qhs_pcie0_cfg",
	.id = SDM845_SLAVE_PCIE_0_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pcie_gen3_cfg = {
	.name = "qhs_pcie_gen3_cfg",
	.id = SDM845_SLAVE_PCIE_1_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pdm = {
	.name = "qhs_pdm",
	.id = SDM845_SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_phy_refgen_south = {
	.name = "qhs_phy_refgen_south",
	.id = SDM845_SLAVE_SOUTH_PHY_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = SDM845_SLAVE_PIMEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_prng = {
	.name = "qhs_prng",
	.id = SDM845_SLAVE_PRNG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SDM845_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qupv3_analrth = {
	.name = "qhs_qupv3_analrth",
	.id = SDM845_SLAVE_BLSP_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qupv3_south = {
	.name = "qhs_qupv3_south",
	.id = SDM845_SLAVE_BLSP_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = SDM845_SLAVE_SDCC_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_sdc4 = {
	.name = "qhs_sdc4",
	.id = SDM845_SLAVE_SDCC_4,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_sanalc_cfg = {
	.name = "qhs_sanalc_cfg",
	.id = SDM845_SLAVE_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDM845_MASTER_SANALC_CFG },
};

static struct qcom_icc_analde qhs_spdm = {
	.name = "qhs_spdm",
	.id = SDM845_SLAVE_SPDM_WRAPPER,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_spss_cfg = {
	.name = "qhs_spss_cfg",
	.id = SDM845_SLAVE_SPSS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SDM845_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tlmm_analrth = {
	.name = "qhs_tlmm_analrth",
	.id = SDM845_SLAVE_TLMM_ANALRTH,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tlmm_south = {
	.name = "qhs_tlmm_south",
	.id = SDM845_SLAVE_TLMM_SOUTH,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tsif = {
	.name = "qhs_tsif",
	.id = SDM845_SLAVE_TSIF,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ufs_card_cfg = {
	.name = "qhs_ufs_card_cfg",
	.id = SDM845_SLAVE_UFS_CARD_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.id = SDM845_SLAVE_UFS_MEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_usb3_0 = {
	.name = "qhs_usb3_0",
	.id = SDM845_SLAVE_USB3_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_usb3_1 = {
	.name = "qhs_usb3_1",
	.id = SDM845_SLAVE_USB3_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = SDM845_SLAVE_VENUS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.id = SDM845_SLAVE_VSENSE_CTRL_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_canalc_a2analc = {
	.name = "qns_canalc_a2analc",
	.id = SDM845_SLAVE_CANALC_A2ANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDM845_MASTER_CANALC_A2ANALC },
};

static struct qcom_icc_analde srvc_canalc = {
	.name = "srvc_canalc",
	.id = SDM845_SLAVE_SERVICE_CANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_llcc = {
	.name = "qhs_llcc",
	.id = SDM845_SLAVE_LLCC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_memanalc = {
	.name = "qhs_memanalc",
	.id = SDM845_SLAVE_MEM_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDM845_MASTER_MEM_ANALC_CFG },
};

static struct qcom_icc_analde qns_gladiator_sodv = {
	.name = "qns_gladiator_sodv",
	.id = SDM845_SLAVE_GANALC_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDM845_MASTER_GANALC_SANALC },
};

static struct qcom_icc_analde qns_ganalc_memanalc = {
	.name = "qns_ganalc_memanalc",
	.id = SDM845_SLAVE_GANALC_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SDM845_MASTER_GANALC_MEM_ANALC },
};

static struct qcom_icc_analde srvc_ganalc = {
	.name = "srvc_ganalc",
	.id = SDM845_SLAVE_SERVICE_GANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde ebi = {
	.name = "ebi",
	.id = SDM845_SLAVE_EBI1,
	.channels = 4,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_mdsp_ms_mpu_cfg = {
	.name = "qhs_mdsp_ms_mpu_cfg",
	.id = SDM845_SLAVE_MSS_PROC_MS_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_apps_io = {
	.name = "qns_apps_io",
	.id = SDM845_SLAVE_MEM_ANALC_GANALC,
	.channels = 1,
	.buswidth = 32,
};

static struct qcom_icc_analde qns_llcc = {
	.name = "qns_llcc",
	.id = SDM845_SLAVE_LLCC,
	.channels = 4,
	.buswidth = 16,
	.num_links = 1,
	.links = { SDM845_MASTER_LLCC },
};

static struct qcom_icc_analde qns_memanalc_sanalc = {
	.name = "qns_memanalc_sanalc",
	.id = SDM845_SLAVE_MEM_ANALC_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDM845_MASTER_MEM_ANALC_SANALC },
};

static struct qcom_icc_analde srvc_memanalc = {
	.name = "srvc_memanalc",
	.id = SDM845_SLAVE_SERVICE_MEM_ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns2_mem_analc = {
	.name = "qns2_mem_analc",
	.id = SDM845_SLAVE_MANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SDM845_MASTER_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qns_mem_analc_hf = {
	.name = "qns_mem_analc_hf",
	.id = SDM845_SLAVE_MANALC_HF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SDM845_MASTER_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde srvc_manalc = {
	.name = "srvc_manalc",
	.id = SDM845_SLAVE_SERVICE_MANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_apss = {
	.name = "qhs_apss",
	.id = SDM845_SLAVE_APPSS,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qns_canalc = {
	.name = "qns_canalc",
	.id = SDM845_SLAVE_SANALC_CANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDM845_MASTER_SANALC_CANALC },
};

static struct qcom_icc_analde qns_memanalc_gc = {
	.name = "qns_memanalc_gc",
	.id = SDM845_SLAVE_SANALC_MEM_ANALC_GC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDM845_MASTER_SANALC_GC_MEM_ANALC },
};

static struct qcom_icc_analde qns_memanalc_sf = {
	.name = "qns_memanalc_sf",
	.id = SDM845_SLAVE_SANALC_MEM_ANALC_SF,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SDM845_MASTER_SANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxs_imem = {
	.name = "qxs_imem",
	.id = SDM845_SLAVE_IMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qxs_pcie = {
	.name = "qxs_pcie",
	.id = SDM845_SLAVE_PCIE_0,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qxs_pcie_gen3 = {
	.name = "qxs_pcie_gen3",
	.id = SDM845_SLAVE_PCIE_1,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qxs_pimem = {
	.name = "qxs_pimem",
	.id = SDM845_SLAVE_PIMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde srvc_sanalc = {
	.name = "srvc_sanalc",
	.id = SDM845_SLAVE_SERVICE_SANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SDM845_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SDM845_SLAVE_TCU,
	.channels = 1,
	.buswidth = 8,
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
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qns_mem_analc_hf },
};

static struct qcom_icc_bcm bcm_sh1 = {
	.name = "SH1",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qns_apps_io },
};

static struct qcom_icc_bcm bcm_mm1 = {
	.name = "MM1",
	.keepalive = true,
	.num_analdes = 7,
	.analdes = { &qxm_camanalc_hf0_uncomp,
		   &qxm_camanalc_hf1_uncomp,
		   &qxm_camanalc_sf_uncomp,
		   &qxm_camanalc_hf0,
		   &qxm_camanalc_hf1,
		   &qxm_mdp0,
		   &qxm_mdp1
	},
};

static struct qcom_icc_bcm bcm_sh2 = {
	.name = "SH2",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qns_memanalc_sanalc },
};

static struct qcom_icc_bcm bcm_mm2 = {
	.name = "MM2",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qns2_mem_analc },
};

static struct qcom_icc_bcm bcm_sh3 = {
	.name = "SH3",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &acm_tcu },
};

static struct qcom_icc_bcm bcm_mm3 = {
	.name = "MM3",
	.keepalive = false,
	.num_analdes = 5,
	.analdes = { &qxm_camanalc_sf, &qxm_rot, &qxm_venus0, &qxm_venus1, &qxm_venus_arm9 },
};

static struct qcom_icc_bcm bcm_sh5 = {
	.name = "SH5",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qnm_apps },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &qns_memanalc_sf },
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.keepalive = false,
	.num_analdes = 47,
	.analdes = { &qhm_spdm,
		   &qhm_tic,
		   &qnm_sanalc,
		   &xm_qdss_dap,
		   &qhs_a1_analc_cfg,
		   &qhs_a2_analc_cfg,
		   &qhs_aop,
		   &qhs_aoss,
		   &qhs_camera_cfg,
		   &qhs_clk_ctl,
		   &qhs_compute_dsp_cfg,
		   &qhs_cpr_cx,
		   &qhs_crypto0_cfg,
		   &qhs_dcc_cfg,
		   &qhs_ddrss_cfg,
		   &qhs_display_cfg,
		   &qhs_glm,
		   &qhs_gpuss_cfg,
		   &qhs_imem_cfg,
		   &qhs_ipa,
		   &qhs_manalc_cfg,
		   &qhs_pcie0_cfg,
		   &qhs_pcie_gen3_cfg,
		   &qhs_pdm,
		   &qhs_phy_refgen_south,
		   &qhs_pimem_cfg,
		   &qhs_prng,
		   &qhs_qdss_cfg,
		   &qhs_qupv3_analrth,
		   &qhs_qupv3_south,
		   &qhs_sdc2,
		   &qhs_sdc4,
		   &qhs_sanalc_cfg,
		   &qhs_spdm,
		   &qhs_spss_cfg,
		   &qhs_tcsr,
		   &qhs_tlmm_analrth,
		   &qhs_tlmm_south,
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

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.keepalive = false,
	.num_analdes = 2,
	.analdes = { &qhm_qup1, &qhm_qup2 },
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
	.analdes = { &qns_memanalc_gc },
};

static struct qcom_icc_bcm bcm_sn3 = {
	.name = "SN3",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qns_canalc },
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qxm_pimem },
};

static struct qcom_icc_bcm bcm_sn5 = {
	.name = "SN5",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &xs_qdss_stm },
};

static struct qcom_icc_bcm bcm_sn6 = {
	.name = "SN6",
	.keepalive = false,
	.num_analdes = 3,
	.analdes = { &qhs_apss, &srvc_sanalc, &xs_sys_tcu_cfg },
};

static struct qcom_icc_bcm bcm_sn7 = {
	.name = "SN7",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qxs_pcie },
};

static struct qcom_icc_bcm bcm_sn8 = {
	.name = "SN8",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qxs_pcie_gen3 },
};

static struct qcom_icc_bcm bcm_sn9 = {
	.name = "SN9",
	.keepalive = false,
	.num_analdes = 2,
	.analdes = { &srvc_aggre1_analc, &qnm_aggre1_analc },
};

static struct qcom_icc_bcm bcm_sn11 = {
	.name = "SN11",
	.keepalive = false,
	.num_analdes = 2,
	.analdes = { &srvc_aggre2_analc, &qnm_aggre2_analc },
};

static struct qcom_icc_bcm bcm_sn12 = {
	.name = "SN12",
	.keepalive = false,
	.num_analdes = 2,
	.analdes = { &qnm_gladiator_sodv, &xm_gic },
};

static struct qcom_icc_bcm bcm_sn14 = {
	.name = "SN14",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qnm_pcie_aanalc },
};

static struct qcom_icc_bcm bcm_sn15 = {
	.name = "SN15",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qnm_memanalc },
};

static struct qcom_icc_bcm * const aggre1_analc_bcms[] = {
	&bcm_sn9,
	&bcm_qup0,
};

static struct qcom_icc_analde * const aggre1_analc_analdes[] = {
	[MASTER_A1ANALC_CFG] = &qhm_a1analc_cfg,
	[MASTER_TSIF] = &qhm_tsif,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_SDCC_4] = &xm_sdc4,
	[MASTER_UFS_CARD] = &xm_ufs_card,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_PCIE_0] = &xm_pcie_0,
	[SLAVE_A1ANALC_SANALC] = &qns_a1analc_sanalc,
	[SLAVE_SERVICE_A1ANALC] = &srvc_aggre1_analc,
	[SLAVE_AANALC_PCIE_A1ANALC_SANALC] = &qns_pcie_a1analc_sanalc,
	[MASTER_QUP_1] = &qhm_qup1,
};

static const struct qcom_icc_desc sdm845_aggre1_analc = {
	.analdes = aggre1_analc_analdes,
	.num_analdes = ARRAY_SIZE(aggre1_analc_analdes),
	.bcms = aggre1_analc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_analc_bcms),
};

static struct qcom_icc_bcm * const aggre2_analc_bcms[] = {
	&bcm_ce0,
	&bcm_sn11,
	&bcm_qup0,
};

static struct qcom_icc_analde * const aggre2_analc_analdes[] = {
	[MASTER_A2ANALC_CFG] = &qhm_a2analc_cfg,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_CANALC_A2ANALC] = &qnm_canalc,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_PCIE_1] = &xm_pcie3_1,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[MASTER_USB3_0] = &xm_usb3_0,
	[MASTER_USB3_1] = &xm_usb3_1,
	[SLAVE_A2ANALC_SANALC] = &qns_a2analc_sanalc,
	[SLAVE_AANALC_PCIE_SANALC] = &qns_pcie_sanalc,
	[SLAVE_SERVICE_A2ANALC] = &srvc_aggre2_analc,
	[MASTER_QUP_2] = &qhm_qup2,
};

static const struct qcom_icc_desc sdm845_aggre2_analc = {
	.analdes = aggre2_analc_analdes,
	.num_analdes = ARRAY_SIZE(aggre2_analc_analdes),
	.bcms = aggre2_analc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_analc_bcms),
};

static struct qcom_icc_bcm * const config_analc_bcms[] = {
	&bcm_cn0,
};

static struct qcom_icc_analde * const config_analc_analdes[] = {
	[MASTER_SPDM] = &qhm_spdm,
	[MASTER_TIC] = &qhm_tic,
	[MASTER_SANALC_CANALC] = &qnm_sanalc,
	[MASTER_QDSS_DAP] = &xm_qdss_dap,
	[SLAVE_A1ANALC_CFG] = &qhs_a1_analc_cfg,
	[SLAVE_A2ANALC_CFG] = &qhs_a2_analc_cfg,
	[SLAVE_AOP] = &qhs_aop,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CDSP_CFG] = &qhs_compute_dsp_cfg,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_DCC_CFG] = &qhs_dcc_cfg,
	[SLAVE_CANALC_DDRSS] = &qhs_ddrss_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_GLM] = &qhs_glm,
	[SLAVE_GFX3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_CANALC_MANALC_CFG] = &qhs_manalc_cfg,
	[SLAVE_PCIE_0_CFG] = &qhs_pcie0_cfg,
	[SLAVE_PCIE_1_CFG] = &qhs_pcie_gen3_cfg,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_SOUTH_PHY_CFG] = &qhs_phy_refgen_south,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_BLSP_2] = &qhs_qupv3_analrth,
	[SLAVE_BLSP_1] = &qhs_qupv3_south,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SDCC_4] = &qhs_sdc4,
	[SLAVE_SANALC_CFG] = &qhs_sanalc_cfg,
	[SLAVE_SPDM_WRAPPER] = &qhs_spdm,
	[SLAVE_SPSS_CFG] = &qhs_spss_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM_ANALRTH] = &qhs_tlmm_analrth,
	[SLAVE_TLMM_SOUTH] = &qhs_tlmm_south,
	[SLAVE_TSIF] = &qhs_tsif,
	[SLAVE_UFS_CARD_CFG] = &qhs_ufs_card_cfg,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB3_0] = &qhs_usb3_0,
	[SLAVE_USB3_1] = &qhs_usb3_1,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_CANALC_A2ANALC] = &qns_canalc_a2analc,
	[SLAVE_SERVICE_CANALC] = &srvc_canalc,
};

static const struct qcom_icc_desc sdm845_config_analc = {
	.analdes = config_analc_analdes,
	.num_analdes = ARRAY_SIZE(config_analc_analdes),
	.bcms = config_analc_bcms,
	.num_bcms = ARRAY_SIZE(config_analc_bcms),
};

static struct qcom_icc_bcm * const dc_analc_bcms[] = {
};

static struct qcom_icc_analde * const dc_analc_analdes[] = {
	[MASTER_CANALC_DC_ANALC] = &qhm_canalc,
	[SLAVE_LLCC_CFG] = &qhs_llcc,
	[SLAVE_MEM_ANALC_CFG] = &qhs_memanalc,
};

static const struct qcom_icc_desc sdm845_dc_analc = {
	.analdes = dc_analc_analdes,
	.num_analdes = ARRAY_SIZE(dc_analc_analdes),
	.bcms = dc_analc_bcms,
	.num_bcms = ARRAY_SIZE(dc_analc_bcms),
};

static struct qcom_icc_bcm * const gladiator_analc_bcms[] = {
};

static struct qcom_icc_analde * const gladiator_analc_analdes[] = {
	[MASTER_APPSS_PROC] = &acm_l3,
	[MASTER_GANALC_CFG] = &pm_ganalc_cfg,
	[SLAVE_GANALC_SANALC] = &qns_gladiator_sodv,
	[SLAVE_GANALC_MEM_ANALC] = &qns_ganalc_memanalc,
	[SLAVE_SERVICE_GANALC] = &srvc_ganalc,
};

static const struct qcom_icc_desc sdm845_gladiator_analc = {
	.analdes = gladiator_analc_analdes,
	.num_analdes = ARRAY_SIZE(gladiator_analc_analdes),
	.bcms = gladiator_analc_bcms,
	.num_bcms = ARRAY_SIZE(gladiator_analc_bcms),
};

static struct qcom_icc_bcm * const mem_analc_bcms[] = {
	&bcm_mc0,
	&bcm_acv,
	&bcm_sh0,
	&bcm_sh1,
	&bcm_sh2,
	&bcm_sh3,
	&bcm_sh5,
};

static struct qcom_icc_analde * const mem_analc_analdes[] = {
	[MASTER_TCU_0] = &acm_tcu,
	[MASTER_MEM_ANALC_CFG] = &qhm_memanalc_cfg,
	[MASTER_GANALC_MEM_ANALC] = &qnm_apps,
	[MASTER_MANALC_HF_MEM_ANALC] = &qnm_manalc_hf,
	[MASTER_MANALC_SF_MEM_ANALC] = &qnm_manalc_sf,
	[MASTER_SANALC_GC_MEM_ANALC] = &qnm_sanalc_gc,
	[MASTER_SANALC_SF_MEM_ANALC] = &qnm_sanalc_sf,
	[MASTER_GFX3D] = &qxm_gpu,
	[SLAVE_MSS_PROC_MS_MPU_CFG] = &qhs_mdsp_ms_mpu_cfg,
	[SLAVE_MEM_ANALC_GANALC] = &qns_apps_io,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_MEM_ANALC_SANALC] = &qns_memanalc_sanalc,
	[SLAVE_SERVICE_MEM_ANALC] = &srvc_memanalc,
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
};

static const struct qcom_icc_desc sdm845_mem_analc = {
	.analdes = mem_analc_analdes,
	.num_analdes = ARRAY_SIZE(mem_analc_analdes),
	.bcms = mem_analc_bcms,
	.num_bcms = ARRAY_SIZE(mem_analc_bcms),
};

static struct qcom_icc_bcm * const mmss_analc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
	&bcm_mm2,
	&bcm_mm3,
};

static struct qcom_icc_analde * const mmss_analc_analdes[] = {
	[MASTER_CANALC_MANALC_CFG] = &qhm_manalc_cfg,
	[MASTER_CAMANALC_HF0] = &qxm_camanalc_hf0,
	[MASTER_CAMANALC_HF1] = &qxm_camanalc_hf1,
	[MASTER_CAMANALC_SF] = &qxm_camanalc_sf,
	[MASTER_MDP0] = &qxm_mdp0,
	[MASTER_MDP1] = &qxm_mdp1,
	[MASTER_ROTATOR] = &qxm_rot,
	[MASTER_VIDEO_P0] = &qxm_venus0,
	[MASTER_VIDEO_P1] = &qxm_venus1,
	[MASTER_VIDEO_PROC] = &qxm_venus_arm9,
	[SLAVE_MANALC_SF_MEM_ANALC] = &qns2_mem_analc,
	[SLAVE_MANALC_HF_MEM_ANALC] = &qns_mem_analc_hf,
	[SLAVE_SERVICE_MANALC] = &srvc_manalc,
	[MASTER_CAMANALC_HF0_UNCOMP] = &qxm_camanalc_hf0_uncomp,
	[MASTER_CAMANALC_HF1_UNCOMP] = &qxm_camanalc_hf1_uncomp,
	[MASTER_CAMANALC_SF_UNCOMP] = &qxm_camanalc_sf_uncomp,
	[SLAVE_CAMANALC_UNCOMP] = &qns_camanalc_uncomp,
};

static const struct qcom_icc_desc sdm845_mmss_analc = {
	.analdes = mmss_analc_analdes,
	.num_analdes = ARRAY_SIZE(mmss_analc_analdes),
	.bcms = mmss_analc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_analc_bcms),
};

static struct qcom_icc_bcm * const system_analc_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn2,
	&bcm_sn3,
	&bcm_sn4,
	&bcm_sn5,
	&bcm_sn6,
	&bcm_sn7,
	&bcm_sn8,
	&bcm_sn9,
	&bcm_sn11,
	&bcm_sn12,
	&bcm_sn14,
	&bcm_sn15,
};

static struct qcom_icc_analde * const system_analc_analdes[] = {
	[MASTER_SANALC_CFG] = &qhm_sanalc_cfg,
	[MASTER_A1ANALC_SANALC] = &qnm_aggre1_analc,
	[MASTER_A2ANALC_SANALC] = &qnm_aggre2_analc,
	[MASTER_GANALC_SANALC] = &qnm_gladiator_sodv,
	[MASTER_MEM_ANALC_SANALC] = &qnm_memanalc,
	[MASTER_AANALC_PCIE_SANALC] = &qnm_pcie_aanalc,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_GIC] = &xm_gic,
	[SLAVE_APPSS] = &qhs_apss,
	[SLAVE_SANALC_CANALC] = &qns_canalc,
	[SLAVE_SANALC_MEM_ANALC_GC] = &qns_memanalc_gc,
	[SLAVE_SANALC_MEM_ANALC_SF] = &qns_memanalc_sf,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_PCIE_0] = &qxs_pcie,
	[SLAVE_PCIE_1] = &qxs_pcie_gen3,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SERVICE_SANALC] = &srvc_sanalc,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sdm845_system_analc = {
	.analdes = system_analc_analdes,
	.num_analdes = ARRAY_SIZE(system_analc_analdes),
	.bcms = system_analc_bcms,
	.num_bcms = ARRAY_SIZE(system_analc_bcms),
};

static const struct of_device_id qanalc_of_match[] = {
	{ .compatible = "qcom,sdm845-aggre1-analc",
	  .data = &sdm845_aggre1_analc},
	{ .compatible = "qcom,sdm845-aggre2-analc",
	  .data = &sdm845_aggre2_analc},
	{ .compatible = "qcom,sdm845-config-analc",
	  .data = &sdm845_config_analc},
	{ .compatible = "qcom,sdm845-dc-analc",
	  .data = &sdm845_dc_analc},
	{ .compatible = "qcom,sdm845-gladiator-analc",
	  .data = &sdm845_gladiator_analc},
	{ .compatible = "qcom,sdm845-mem-analc",
	  .data = &sdm845_mem_analc},
	{ .compatible = "qcom,sdm845-mmss-analc",
	  .data = &sdm845_mmss_analc},
	{ .compatible = "qcom,sdm845-system-analc",
	  .data = &sdm845_system_analc},
	{ }
};
MODULE_DEVICE_TABLE(of, qanalc_of_match);

static struct platform_driver qanalc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove_new = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qanalc-sdm845",
		.of_match_table = qanalc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(qanalc_driver);

MODULE_AUTHOR("David Dai <daidavid1@codeaurora.org>");
MODULE_DESCRIPTION("Qualcomm sdm845 AnalC driver");
MODULE_LICENSE("GPL v2");
