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
#include <dt-bindings/interconnect/qcom,sm8150.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"
#include "sm8150.h"

static struct qcom_icc_analde qhm_a1analc_cfg = {
	.name = "qhm_a1analc_cfg",
	.id = SM8150_MASTER_A1ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8150_SLAVE_SERVICE_A1ANALC },
};

static struct qcom_icc_analde qhm_qup0 = {
	.name = "qhm_qup0",
	.id = SM8150_MASTER_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8150_A1ANALC_SANALC_SLV },
};

static struct qcom_icc_analde xm_emac = {
	.name = "xm_emac",
	.id = SM8150_MASTER_EMAC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8150_A1ANALC_SANALC_SLV },
};

static struct qcom_icc_analde xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.id = SM8150_MASTER_UFS_MEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8150_A1ANALC_SANALC_SLV },
};

static struct qcom_icc_analde xm_usb3_0 = {
	.name = "xm_usb3_0",
	.id = SM8150_MASTER_USB3,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8150_A1ANALC_SANALC_SLV },
};

static struct qcom_icc_analde xm_usb3_1 = {
	.name = "xm_usb3_1",
	.id = SM8150_MASTER_USB3_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8150_A1ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qhm_a2analc_cfg = {
	.name = "qhm_a2analc_cfg",
	.id = SM8150_MASTER_A2ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8150_SLAVE_SERVICE_A2ANALC },
};

static struct qcom_icc_analde qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = SM8150_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8150_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qhm_qspi = {
	.name = "qhm_qspi",
	.id = SM8150_MASTER_QSPI,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8150_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qhm_qup1 = {
	.name = "qhm_qup1",
	.id = SM8150_MASTER_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8150_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qhm_qup2 = {
	.name = "qhm_qup2",
	.id = SM8150_MASTER_QUP_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8150_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qhm_sensorss_ahb = {
	.name = "qhm_sensorss_ahb",
	.id = SM8150_MASTER_SENSORS_AHB,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8150_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qhm_tsif = {
	.name = "qhm_tsif",
	.id = SM8150_MASTER_TSIF,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8150_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qnm_canalc = {
	.name = "qnm_canalc",
	.id = SM8150_MASTER_CANALC_A2ANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8150_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qxm_crypto = {
	.name = "qxm_crypto",
	.id = SM8150_MASTER_CRYPTO_CORE_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8150_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qxm_ipa = {
	.name = "qxm_ipa",
	.id = SM8150_MASTER_IPA,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8150_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde xm_pcie3_0 = {
	.name = "xm_pcie3_0",
	.id = SM8150_MASTER_PCIE,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8150_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde xm_pcie3_1 = {
	.name = "xm_pcie3_1",
	.id = SM8150_MASTER_PCIE_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8150_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde xm_qdss_etr = {
	.name = "xm_qdss_etr",
	.id = SM8150_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8150_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde xm_sdc2 = {
	.name = "xm_sdc2",
	.id = SM8150_MASTER_SDCC_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8150_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde xm_sdc4 = {
	.name = "xm_sdc4",
	.id = SM8150_MASTER_SDCC_4,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8150_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qxm_camanalc_hf0_uncomp = {
	.name = "qxm_camanalc_hf0_uncomp",
	.id = SM8150_MASTER_CAMANALC_HF0_UNCOMP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8150_SLAVE_CAMANALC_UNCOMP },
};

static struct qcom_icc_analde qxm_camanalc_hf1_uncomp = {
	.name = "qxm_camanalc_hf1_uncomp",
	.id = SM8150_MASTER_CAMANALC_HF1_UNCOMP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8150_SLAVE_CAMANALC_UNCOMP },
};

static struct qcom_icc_analde qxm_camanalc_sf_uncomp = {
	.name = "qxm_camanalc_sf_uncomp",
	.id = SM8150_MASTER_CAMANALC_SF_UNCOMP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8150_SLAVE_CAMANALC_UNCOMP },
};

static struct qcom_icc_analde qnm_npu = {
	.name = "qnm_npu",
	.id = SM8150_MASTER_NPU,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8150_SLAVE_CDSP_MEM_ANALC },
};

static struct qcom_icc_analde qhm_spdm = {
	.name = "qhm_spdm",
	.id = SM8150_MASTER_SPDM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8150_SLAVE_CANALC_A2ANALC },
};

static struct qcom_icc_analde qnm_sanalc = {
	.name = "qnm_sanalc",
	.id = SM8150_SANALC_CANALC_MAS,
	.channels = 1,
	.buswidth = 8,
	.num_links = 50,
	.links = { SM8150_SLAVE_TLMM_SOUTH,
		   SM8150_SLAVE_CDSP_CFG,
		   SM8150_SLAVE_SPSS_CFG,
		   SM8150_SLAVE_CAMERA_CFG,
		   SM8150_SLAVE_SDCC_4,
		   SM8150_SLAVE_SDCC_2,
		   SM8150_SLAVE_CANALC_MANALC_CFG,
		   SM8150_SLAVE_EMAC_CFG,
		   SM8150_SLAVE_UFS_MEM_CFG,
		   SM8150_SLAVE_TLMM_EAST,
		   SM8150_SLAVE_SSC_CFG,
		   SM8150_SLAVE_SANALC_CFG,
		   SM8150_SLAVE_ANALRTH_PHY_CFG,
		   SM8150_SLAVE_QUP_0,
		   SM8150_SLAVE_GLM,
		   SM8150_SLAVE_PCIE_1_CFG,
		   SM8150_SLAVE_A2ANALC_CFG,
		   SM8150_SLAVE_QDSS_CFG,
		   SM8150_SLAVE_DISPLAY_CFG,
		   SM8150_SLAVE_TCSR,
		   SM8150_SLAVE_CANALC_DDRSS,
		   SM8150_SLAVE_RBCPR_MMCX_CFG,
		   SM8150_SLAVE_NPU_CFG,
		   SM8150_SLAVE_PCIE_0_CFG,
		   SM8150_SLAVE_GRAPHICS_3D_CFG,
		   SM8150_SLAVE_VENUS_CFG,
		   SM8150_SLAVE_TSIF,
		   SM8150_SLAVE_IPA_CFG,
		   SM8150_SLAVE_CLK_CTL,
		   SM8150_SLAVE_AOP,
		   SM8150_SLAVE_QUP_1,
		   SM8150_SLAVE_AHB2PHY_SOUTH,
		   SM8150_SLAVE_USB3_1,
		   SM8150_SLAVE_SERVICE_CANALC,
		   SM8150_SLAVE_UFS_CARD_CFG,
		   SM8150_SLAVE_QUP_2,
		   SM8150_SLAVE_RBCPR_CX_CFG,
		   SM8150_SLAVE_TLMM_WEST,
		   SM8150_SLAVE_A1ANALC_CFG,
		   SM8150_SLAVE_AOSS,
		   SM8150_SLAVE_PRNG,
		   SM8150_SLAVE_VSENSE_CTRL_CFG,
		   SM8150_SLAVE_QSPI,
		   SM8150_SLAVE_USB3,
		   SM8150_SLAVE_SPDM_WRAPPER,
		   SM8150_SLAVE_CRYPTO_0_CFG,
		   SM8150_SLAVE_PIMEM_CFG,
		   SM8150_SLAVE_TLMM_ANALRTH,
		   SM8150_SLAVE_RBCPR_MX_CFG,
		   SM8150_SLAVE_IMEM_CFG
	},
};

static struct qcom_icc_analde xm_qdss_dap = {
	.name = "xm_qdss_dap",
	.id = SM8150_MASTER_QDSS_DAP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 51,
	.links = { SM8150_SLAVE_TLMM_SOUTH,
		   SM8150_SLAVE_CDSP_CFG,
		   SM8150_SLAVE_SPSS_CFG,
		   SM8150_SLAVE_CAMERA_CFG,
		   SM8150_SLAVE_SDCC_4,
		   SM8150_SLAVE_SDCC_2,
		   SM8150_SLAVE_CANALC_MANALC_CFG,
		   SM8150_SLAVE_EMAC_CFG,
		   SM8150_SLAVE_UFS_MEM_CFG,
		   SM8150_SLAVE_TLMM_EAST,
		   SM8150_SLAVE_SSC_CFG,
		   SM8150_SLAVE_SANALC_CFG,
		   SM8150_SLAVE_ANALRTH_PHY_CFG,
		   SM8150_SLAVE_QUP_0,
		   SM8150_SLAVE_GLM,
		   SM8150_SLAVE_PCIE_1_CFG,
		   SM8150_SLAVE_A2ANALC_CFG,
		   SM8150_SLAVE_QDSS_CFG,
		   SM8150_SLAVE_DISPLAY_CFG,
		   SM8150_SLAVE_TCSR,
		   SM8150_SLAVE_CANALC_DDRSS,
		   SM8150_SLAVE_CANALC_A2ANALC,
		   SM8150_SLAVE_RBCPR_MMCX_CFG,
		   SM8150_SLAVE_NPU_CFG,
		   SM8150_SLAVE_PCIE_0_CFG,
		   SM8150_SLAVE_GRAPHICS_3D_CFG,
		   SM8150_SLAVE_VENUS_CFG,
		   SM8150_SLAVE_TSIF,
		   SM8150_SLAVE_IPA_CFG,
		   SM8150_SLAVE_CLK_CTL,
		   SM8150_SLAVE_AOP,
		   SM8150_SLAVE_QUP_1,
		   SM8150_SLAVE_AHB2PHY_SOUTH,
		   SM8150_SLAVE_USB3_1,
		   SM8150_SLAVE_SERVICE_CANALC,
		   SM8150_SLAVE_UFS_CARD_CFG,
		   SM8150_SLAVE_QUP_2,
		   SM8150_SLAVE_RBCPR_CX_CFG,
		   SM8150_SLAVE_TLMM_WEST,
		   SM8150_SLAVE_A1ANALC_CFG,
		   SM8150_SLAVE_AOSS,
		   SM8150_SLAVE_PRNG,
		   SM8150_SLAVE_VSENSE_CTRL_CFG,
		   SM8150_SLAVE_QSPI,
		   SM8150_SLAVE_USB3,
		   SM8150_SLAVE_SPDM_WRAPPER,
		   SM8150_SLAVE_CRYPTO_0_CFG,
		   SM8150_SLAVE_PIMEM_CFG,
		   SM8150_SLAVE_TLMM_ANALRTH,
		   SM8150_SLAVE_RBCPR_MX_CFG,
		   SM8150_SLAVE_IMEM_CFG
	},
};

static struct qcom_icc_analde qhm_canalc_dc_analc = {
	.name = "qhm_canalc_dc_analc",
	.id = SM8150_MASTER_CANALC_DC_ANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.links = { SM8150_SLAVE_GEM_ANALC_CFG,
		   SM8150_SLAVE_LLCC_CFG
	},
};

static struct qcom_icc_analde acm_apps = {
	.name = "acm_apps",
	.id = SM8150_MASTER_AMPSS_M0,
	.channels = 2,
	.buswidth = 32,
	.num_links = 3,
	.links = { SM8150_SLAVE_ECC,
		   SM8150_SLAVE_LLCC,
		   SM8150_SLAVE_GEM_ANALC_SANALC
	},
};

static struct qcom_icc_analde acm_gpu_tcu = {
	.name = "acm_gpu_tcu",
	.id = SM8150_MASTER_GPU_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM8150_SLAVE_LLCC,
		   SM8150_SLAVE_GEM_ANALC_SANALC
	},
};

static struct qcom_icc_analde acm_sys_tcu = {
	.name = "acm_sys_tcu",
	.id = SM8150_MASTER_SYS_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM8150_SLAVE_LLCC,
		   SM8150_SLAVE_GEM_ANALC_SANALC
	},
};

static struct qcom_icc_analde qhm_gemanalc_cfg = {
	.name = "qhm_gemanalc_cfg",
	.id = SM8150_MASTER_GEM_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.links = { SM8150_SLAVE_SERVICE_GEM_ANALC,
		   SM8150_SLAVE_MSS_PROC_MS_MPU_CFG
	},
};

static struct qcom_icc_analde qnm_cmpanalc = {
	.name = "qnm_cmpanalc",
	.id = SM8150_MASTER_COMPUTE_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 3,
	.links = { SM8150_SLAVE_ECC,
		   SM8150_SLAVE_LLCC,
		   SM8150_SLAVE_GEM_ANALC_SANALC
	},
};

static struct qcom_icc_analde qnm_gpu = {
	.name = "qnm_gpu",
	.id = SM8150_MASTER_GRAPHICS_3D,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM8150_SLAVE_LLCC,
		   SM8150_SLAVE_GEM_ANALC_SANALC
	},
};

static struct qcom_icc_analde qnm_manalc_hf = {
	.name = "qnm_manalc_hf",
	.id = SM8150_MASTER_MANALC_HF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8150_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_manalc_sf = {
	.name = "qnm_manalc_sf",
	.id = SM8150_MASTER_MANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM8150_SLAVE_LLCC,
		   SM8150_SLAVE_GEM_ANALC_SANALC
	},
};

static struct qcom_icc_analde qnm_pcie = {
	.name = "qnm_pcie",
	.id = SM8150_MASTER_GEM_ANALC_PCIE_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 2,
	.links = { SM8150_SLAVE_LLCC,
		   SM8150_SLAVE_GEM_ANALC_SANALC
	},
};

static struct qcom_icc_analde qnm_sanalc_gc = {
	.name = "qnm_sanalc_gc",
	.id = SM8150_MASTER_SANALC_GC_MEM_ANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8150_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_sanalc_sf = {
	.name = "qnm_sanalc_sf",
	.id = SM8150_MASTER_SANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8150_SLAVE_LLCC },
};

static struct qcom_icc_analde qxm_ecc = {
	.name = "qxm_ecc",
	.id = SM8150_MASTER_ECC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8150_SLAVE_LLCC },
};

static struct qcom_icc_analde llcc_mc = {
	.name = "llcc_mc",
	.id = SM8150_MASTER_LLCC,
	.channels = 4,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8150_SLAVE_EBI_CH0 },
};

static struct qcom_icc_analde qhm_manalc_cfg = {
	.name = "qhm_manalc_cfg",
	.id = SM8150_MASTER_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8150_SLAVE_SERVICE_MANALC },
};

static struct qcom_icc_analde qxm_camanalc_hf0 = {
	.name = "qxm_camanalc_hf0",
	.id = SM8150_MASTER_CAMANALC_HF0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8150_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_camanalc_hf1 = {
	.name = "qxm_camanalc_hf1",
	.id = SM8150_MASTER_CAMANALC_HF1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8150_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_camanalc_sf = {
	.name = "qxm_camanalc_sf",
	.id = SM8150_MASTER_CAMANALC_SF,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8150_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_mdp0 = {
	.name = "qxm_mdp0",
	.id = SM8150_MASTER_MDP_PORT0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8150_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_mdp1 = {
	.name = "qxm_mdp1",
	.id = SM8150_MASTER_MDP_PORT1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8150_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_rot = {
	.name = "qxm_rot",
	.id = SM8150_MASTER_ROTATOR,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8150_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_venus0 = {
	.name = "qxm_venus0",
	.id = SM8150_MASTER_VIDEO_P0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8150_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_venus1 = {
	.name = "qxm_venus1",
	.id = SM8150_MASTER_VIDEO_P1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8150_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_venus_arm9 = {
	.name = "qxm_venus_arm9",
	.id = SM8150_MASTER_VIDEO_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8150_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qhm_sanalc_cfg = {
	.name = "qhm_sanalc_cfg",
	.id = SM8150_MASTER_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8150_SLAVE_SERVICE_SANALC },
};

static struct qcom_icc_analde qnm_aggre1_analc = {
	.name = "qnm_aggre1_analc",
	.id = SM8150_A1ANALC_SANALC_MAS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 6,
	.links = { SM8150_SLAVE_SANALC_GEM_ANALC_SF,
		   SM8150_SLAVE_PIMEM,
		   SM8150_SLAVE_OCIMEM,
		   SM8150_SLAVE_APPSS,
		   SM8150_SANALC_CANALC_SLV,
		   SM8150_SLAVE_QDSS_STM
	},
};

static struct qcom_icc_analde qnm_aggre2_analc = {
	.name = "qnm_aggre2_analc",
	.id = SM8150_A2ANALC_SANALC_MAS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 9,
	.links = { SM8150_SLAVE_SANALC_GEM_ANALC_SF,
		   SM8150_SLAVE_PIMEM,
		   SM8150_SLAVE_OCIMEM,
		   SM8150_SLAVE_APPSS,
		   SM8150_SANALC_CANALC_SLV,
		   SM8150_SLAVE_PCIE_0,
		   SM8150_SLAVE_PCIE_1,
		   SM8150_SLAVE_TCU,
		   SM8150_SLAVE_QDSS_STM
	},
};

static struct qcom_icc_analde qnm_gemanalc = {
	.name = "qnm_gemanalc",
	.id = SM8150_MASTER_GEM_ANALC_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 6,
	.links = { SM8150_SLAVE_PIMEM,
		   SM8150_SLAVE_OCIMEM,
		   SM8150_SLAVE_APPSS,
		   SM8150_SANALC_CANALC_SLV,
		   SM8150_SLAVE_TCU,
		   SM8150_SLAVE_QDSS_STM
	},
};

static struct qcom_icc_analde qxm_pimem = {
	.name = "qxm_pimem",
	.id = SM8150_MASTER_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM8150_SLAVE_SANALC_GEM_ANALC_GC,
		   SM8150_SLAVE_OCIMEM
	},
};

static struct qcom_icc_analde xm_gic = {
	.name = "xm_gic",
	.id = SM8150_MASTER_GIC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM8150_SLAVE_SANALC_GEM_ANALC_GC,
		   SM8150_SLAVE_OCIMEM
	},
};

static struct qcom_icc_analde qns_a1analc_sanalc = {
	.name = "qns_a1analc_sanalc",
	.id = SM8150_A1ANALC_SANALC_SLV,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8150_A1ANALC_SANALC_MAS },
};

static struct qcom_icc_analde srvc_aggre1_analc = {
	.name = "srvc_aggre1_analc",
	.id = SM8150_SLAVE_SERVICE_A1ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_a2analc_sanalc = {
	.name = "qns_a2analc_sanalc",
	.id = SM8150_A2ANALC_SANALC_SLV,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8150_A2ANALC_SANALC_MAS },
};

static struct qcom_icc_analde qns_pcie_mem_analc = {
	.name = "qns_pcie_mem_analc",
	.id = SM8150_SLAVE_AANALC_PCIE_GEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8150_MASTER_GEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde srvc_aggre2_analc = {
	.name = "srvc_aggre2_analc",
	.id = SM8150_SLAVE_SERVICE_A2ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_camanalc_uncomp = {
	.name = "qns_camanalc_uncomp",
	.id = SM8150_SLAVE_CAMANALC_UNCOMP,
	.channels = 1,
	.buswidth = 32,
};

static struct qcom_icc_analde qns_cdsp_mem_analc = {
	.name = "qns_cdsp_mem_analc",
	.id = SM8150_SLAVE_CDSP_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8150_MASTER_COMPUTE_ANALC },
};

static struct qcom_icc_analde qhs_a1_analc_cfg = {
	.name = "qhs_a1_analc_cfg",
	.id = SM8150_SLAVE_A1ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8150_MASTER_A1ANALC_CFG },
};

static struct qcom_icc_analde qhs_a2_analc_cfg = {
	.name = "qhs_a2_analc_cfg",
	.id = SM8150_SLAVE_A2ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8150_MASTER_A2ANALC_CFG },
};

static struct qcom_icc_analde qhs_ahb2phy_south = {
	.name = "qhs_ahb2phy_south",
	.id = SM8150_SLAVE_AHB2PHY_SOUTH,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_aop = {
	.name = "qhs_aop",
	.id = SM8150_SLAVE_AOP,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_aoss = {
	.name = "qhs_aoss",
	.id = SM8150_SLAVE_AOSS,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_camera_cfg = {
	.name = "qhs_camera_cfg",
	.id = SM8150_SLAVE_CAMERA_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SM8150_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_compute_dsp = {
	.name = "qhs_compute_dsp",
	.id = SM8150_SLAVE_CDSP_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = SM8150_SLAVE_RBCPR_CX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cpr_mmcx = {
	.name = "qhs_cpr_mmcx",
	.id = SM8150_SLAVE_RBCPR_MMCX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cpr_mx = {
	.name = "qhs_cpr_mx",
	.id = SM8150_SLAVE_RBCPR_MX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = SM8150_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ddrss_cfg = {
	.name = "qhs_ddrss_cfg",
	.id = SM8150_SLAVE_CANALC_DDRSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8150_MASTER_CANALC_DC_ANALC },
};

static struct qcom_icc_analde qhs_display_cfg = {
	.name = "qhs_display_cfg",
	.id = SM8150_SLAVE_DISPLAY_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_emac_cfg = {
	.name = "qhs_emac_cfg",
	.id = SM8150_SLAVE_EMAC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_glm = {
	.name = "qhs_glm",
	.id = SM8150_SLAVE_GLM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.id = SM8150_SLAVE_GRAPHICS_3D_CFG,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SM8150_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ipa = {
	.name = "qhs_ipa",
	.id = SM8150_SLAVE_IPA_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_manalc_cfg = {
	.name = "qhs_manalc_cfg",
	.id = SM8150_SLAVE_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8150_MASTER_CANALC_MANALC_CFG },
};

static struct qcom_icc_analde qhs_npu_cfg = {
	.name = "qhs_npu_cfg",
	.id = SM8150_SLAVE_NPU_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pcie0_cfg = {
	.name = "qhs_pcie0_cfg",
	.id = SM8150_SLAVE_PCIE_0_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pcie1_cfg = {
	.name = "qhs_pcie1_cfg",
	.id = SM8150_SLAVE_PCIE_1_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_phy_refgen_analrth = {
	.name = "qhs_phy_refgen_analrth",
	.id = SM8150_SLAVE_ANALRTH_PHY_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = SM8150_SLAVE_PIMEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_prng = {
	.name = "qhs_prng",
	.id = SM8150_SLAVE_PRNG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SM8150_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qspi = {
	.name = "qhs_qspi",
	.id = SM8150_SLAVE_QSPI,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qupv3_east = {
	.name = "qhs_qupv3_east",
	.id = SM8150_SLAVE_QUP_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qupv3_analrth = {
	.name = "qhs_qupv3_analrth",
	.id = SM8150_SLAVE_QUP_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qupv3_south = {
	.name = "qhs_qupv3_south",
	.id = SM8150_SLAVE_QUP_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = SM8150_SLAVE_SDCC_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_sdc4 = {
	.name = "qhs_sdc4",
	.id = SM8150_SLAVE_SDCC_4,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_sanalc_cfg = {
	.name = "qhs_sanalc_cfg",
	.id = SM8150_SLAVE_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8150_MASTER_SANALC_CFG },
};

static struct qcom_icc_analde qhs_spdm = {
	.name = "qhs_spdm",
	.id = SM8150_SLAVE_SPDM_WRAPPER,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_spss_cfg = {
	.name = "qhs_spss_cfg",
	.id = SM8150_SLAVE_SPSS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ssc_cfg = {
	.name = "qhs_ssc_cfg",
	.id = SM8150_SLAVE_SSC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SM8150_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tlmm_east = {
	.name = "qhs_tlmm_east",
	.id = SM8150_SLAVE_TLMM_EAST,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tlmm_analrth = {
	.name = "qhs_tlmm_analrth",
	.id = SM8150_SLAVE_TLMM_ANALRTH,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tlmm_south = {
	.name = "qhs_tlmm_south",
	.id = SM8150_SLAVE_TLMM_SOUTH,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tlmm_west = {
	.name = "qhs_tlmm_west",
	.id = SM8150_SLAVE_TLMM_WEST,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tsif = {
	.name = "qhs_tsif",
	.id = SM8150_SLAVE_TSIF,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ufs_card_cfg = {
	.name = "qhs_ufs_card_cfg",
	.id = SM8150_SLAVE_UFS_CARD_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.id = SM8150_SLAVE_UFS_MEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_usb3_0 = {
	.name = "qhs_usb3_0",
	.id = SM8150_SLAVE_USB3,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_usb3_1 = {
	.name = "qhs_usb3_1",
	.id = SM8150_SLAVE_USB3_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = SM8150_SLAVE_VENUS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.id = SM8150_SLAVE_VSENSE_CTRL_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_canalc_a2analc = {
	.name = "qns_canalc_a2analc",
	.id = SM8150_SLAVE_CANALC_A2ANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8150_MASTER_CANALC_A2ANALC },
};

static struct qcom_icc_analde srvc_canalc = {
	.name = "srvc_canalc",
	.id = SM8150_SLAVE_SERVICE_CANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_llcc = {
	.name = "qhs_llcc",
	.id = SM8150_SLAVE_LLCC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_memanalc = {
	.name = "qhs_memanalc",
	.id = SM8150_SLAVE_GEM_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM8150_MASTER_GEM_ANALC_CFG },
};

static struct qcom_icc_analde qhs_mdsp_ms_mpu_cfg = {
	.name = "qhs_mdsp_ms_mpu_cfg",
	.id = SM8150_SLAVE_MSS_PROC_MS_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_ecc = {
	.name = "qns_ecc",
	.id = SM8150_SLAVE_ECC,
	.channels = 1,
	.buswidth = 32,
};

static struct qcom_icc_analde qns_gem_analc_sanalc = {
	.name = "qns_gem_analc_sanalc",
	.id = SM8150_SLAVE_GEM_ANALC_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8150_MASTER_GEM_ANALC_SANALC },
};

static struct qcom_icc_analde qns_llcc = {
	.name = "qns_llcc",
	.id = SM8150_SLAVE_LLCC,
	.channels = 4,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8150_MASTER_LLCC },
};

static struct qcom_icc_analde srvc_gemanalc = {
	.name = "srvc_gemanalc",
	.id = SM8150_SLAVE_SERVICE_GEM_ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde ebi = {
	.name = "ebi",
	.id = SM8150_SLAVE_EBI_CH0,
	.channels = 4,
	.buswidth = 4,
};

static struct qcom_icc_analde qns2_mem_analc = {
	.name = "qns2_mem_analc",
	.id = SM8150_SLAVE_MANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8150_MASTER_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qns_mem_analc_hf = {
	.name = "qns_mem_analc_hf",
	.id = SM8150_SLAVE_MANALC_HF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM8150_MASTER_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde srvc_manalc = {
	.name = "srvc_manalc",
	.id = SM8150_SLAVE_SERVICE_MANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_apss = {
	.name = "qhs_apss",
	.id = SM8150_SLAVE_APPSS,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qns_canalc = {
	.name = "qns_canalc",
	.id = SM8150_SANALC_CANALC_SLV,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8150_SANALC_CANALC_MAS },
};

static struct qcom_icc_analde qns_gemanalc_gc = {
	.name = "qns_gemanalc_gc",
	.id = SM8150_SLAVE_SANALC_GEM_ANALC_GC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM8150_MASTER_SANALC_GC_MEM_ANALC },
};

static struct qcom_icc_analde qns_gemanalc_sf = {
	.name = "qns_gemanalc_sf",
	.id = SM8150_SLAVE_SANALC_GEM_ANALC_SF,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM8150_MASTER_SANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxs_imem = {
	.name = "qxs_imem",
	.id = SM8150_SLAVE_OCIMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qxs_pimem = {
	.name = "qxs_pimem",
	.id = SM8150_SLAVE_PIMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde srvc_sanalc = {
	.name = "srvc_sanalc",
	.id = SM8150_SLAVE_SERVICE_SANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde xs_pcie_0 = {
	.name = "xs_pcie_0",
	.id = SM8150_SLAVE_PCIE_0,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde xs_pcie_1 = {
	.name = "xs_pcie_1",
	.id = SM8150_SLAVE_PCIE_1,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SM8150_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SM8150_SLAVE_TCU,
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
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &qns_mem_analc_hf },
};

static struct qcom_icc_bcm bcm_mm1 = {
	.name = "MM1",
	.keepalive = false,
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
	.analdes = { &qns_gem_analc_sanalc },
};

static struct qcom_icc_bcm bcm_mm2 = {
	.name = "MM2",
	.keepalive = false,
	.num_analdes = 2,
	.analdes = { &qxm_camanalc_sf, &qns2_mem_analc },
};

static struct qcom_icc_bcm bcm_sh3 = {
	.name = "SH3",
	.keepalive = false,
	.num_analdes = 2,
	.analdes = { &acm_gpu_tcu, &acm_sys_tcu },
};

static struct qcom_icc_bcm bcm_mm3 = {
	.name = "MM3",
	.keepalive = false,
	.num_analdes = 4,
	.analdes = { &qxm_rot, &qxm_venus0, &qxm_venus1, &qxm_venus_arm9 },
};

static struct qcom_icc_bcm bcm_sh4 = {
	.name = "SH4",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qnm_cmpanalc },
};

static struct qcom_icc_bcm bcm_sh5 = {
	.name = "SH5",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &acm_apps },
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

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qxs_imem },
};

static struct qcom_icc_bcm bcm_co1 = {
	.name = "CO1",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qnm_npu },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.keepalive = true,
	.num_analdes = 53,
	.analdes = { &qhm_spdm,
		   &qnm_sanalc,
		   &qhs_a1_analc_cfg,
		   &qhs_a2_analc_cfg,
		   &qhs_ahb2phy_south,
		   &qhs_aop,
		   &qhs_aoss,
		   &qhs_camera_cfg,
		   &qhs_clk_ctl,
		   &qhs_compute_dsp,
		   &qhs_cpr_cx,
		   &qhs_cpr_mmcx,
		   &qhs_cpr_mx,
		   &qhs_crypto0_cfg,
		   &qhs_ddrss_cfg,
		   &qhs_display_cfg,
		   &qhs_emac_cfg,
		   &qhs_glm,
		   &qhs_gpuss_cfg,
		   &qhs_imem_cfg,
		   &qhs_ipa,
		   &qhs_manalc_cfg,
		   &qhs_npu_cfg,
		   &qhs_pcie0_cfg,
		   &qhs_pcie1_cfg,
		   &qhs_phy_refgen_analrth,
		   &qhs_pimem_cfg,
		   &qhs_prng,
		   &qhs_qdss_cfg,
		   &qhs_qspi,
		   &qhs_qupv3_east,
		   &qhs_qupv3_analrth,
		   &qhs_qupv3_south,
		   &qhs_sdc2,
		   &qhs_sdc4,
		   &qhs_sanalc_cfg,
		   &qhs_spdm,
		   &qhs_spss_cfg,
		   &qhs_ssc_cfg,
		   &qhs_tcsr,
		   &qhs_tlmm_east,
		   &qhs_tlmm_analrth,
		   &qhs_tlmm_south,
		   &qhs_tlmm_west,
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
	.num_analdes = 3,
	.analdes = { &qhm_qup0, &qhm_qup1, &qhm_qup2 },
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
	.num_analdes = 3,
	.analdes = { &srvc_aggre1_analc, &srvc_aggre2_analc, &qns_canalc },
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qxs_pimem },
};

static struct qcom_icc_bcm bcm_sn5 = {
	.name = "SN5",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &xs_qdss_stm },
};

static struct qcom_icc_bcm bcm_sn8 = {
	.name = "SN8",
	.keepalive = false,
	.num_analdes = 2,
	.analdes = { &xs_pcie_0, &xs_pcie_1 },
};

static struct qcom_icc_bcm bcm_sn9 = {
	.name = "SN9",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qnm_aggre1_analc },
};

static struct qcom_icc_bcm bcm_sn11 = {
	.name = "SN11",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qnm_aggre2_analc },
};

static struct qcom_icc_bcm bcm_sn12 = {
	.name = "SN12",
	.keepalive = false,
	.num_analdes = 2,
	.analdes = { &qxm_pimem, &xm_gic },
};

static struct qcom_icc_bcm bcm_sn14 = {
	.name = "SN14",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qns_pcie_mem_analc },
};

static struct qcom_icc_bcm bcm_sn15 = {
	.name = "SN15",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qnm_gemanalc },
};

static struct qcom_icc_bcm * const aggre1_analc_bcms[] = {
	&bcm_qup0,
	&bcm_sn3,
};

static struct qcom_icc_analde * const aggre1_analc_analdes[] = {
	[MASTER_A1ANALC_CFG] = &qhm_a1analc_cfg,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_EMAC] = &xm_emac,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_USB3] = &xm_usb3_0,
	[MASTER_USB3_1] = &xm_usb3_1,
	[A1ANALC_SANALC_SLV] = &qns_a1analc_sanalc,
	[SLAVE_SERVICE_A1ANALC] = &srvc_aggre1_analc,
};

static const struct qcom_icc_desc sm8150_aggre1_analc = {
	.analdes = aggre1_analc_analdes,
	.num_analdes = ARRAY_SIZE(aggre1_analc_analdes),
	.bcms = aggre1_analc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_analc_bcms),
};

static struct qcom_icc_bcm * const aggre2_analc_bcms[] = {
	&bcm_ce0,
	&bcm_qup0,
	&bcm_sn14,
	&bcm_sn3,
};

static struct qcom_icc_analde * const aggre2_analc_analdes[] = {
	[MASTER_A2ANALC_CFG] = &qhm_a2analc_cfg,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QSPI] = &qhm_qspi,
	[MASTER_QUP_1] = &qhm_qup1,
	[MASTER_QUP_2] = &qhm_qup2,
	[MASTER_SENSORS_AHB] = &qhm_sensorss_ahb,
	[MASTER_TSIF] = &qhm_tsif,
	[MASTER_CANALC_A2ANALC] = &qnm_canalc,
	[MASTER_CRYPTO_CORE_0] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_PCIE] = &xm_pcie3_0,
	[MASTER_PCIE_1] = &xm_pcie3_1,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_SDCC_4] = &xm_sdc4,
	[A2ANALC_SANALC_SLV] = &qns_a2analc_sanalc,
	[SLAVE_AANALC_PCIE_GEM_ANALC] = &qns_pcie_mem_analc,
	[SLAVE_SERVICE_A2ANALC] = &srvc_aggre2_analc,
};

static const struct qcom_icc_desc sm8150_aggre2_analc = {
	.analdes = aggre2_analc_analdes,
	.num_analdes = ARRAY_SIZE(aggre2_analc_analdes),
	.bcms = aggre2_analc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_analc_bcms),
};

static struct qcom_icc_bcm * const camanalc_virt_bcms[] = {
	&bcm_mm1,
};

static struct qcom_icc_analde * const camanalc_virt_analdes[] = {
	[MASTER_CAMANALC_HF0_UNCOMP] = &qxm_camanalc_hf0_uncomp,
	[MASTER_CAMANALC_HF1_UNCOMP] = &qxm_camanalc_hf1_uncomp,
	[MASTER_CAMANALC_SF_UNCOMP] = &qxm_camanalc_sf_uncomp,
	[SLAVE_CAMANALC_UNCOMP] = &qns_camanalc_uncomp,
};

static const struct qcom_icc_desc sm8150_camanalc_virt = {
	.analdes = camanalc_virt_analdes,
	.num_analdes = ARRAY_SIZE(camanalc_virt_analdes),
	.bcms = camanalc_virt_bcms,
	.num_bcms = ARRAY_SIZE(camanalc_virt_bcms),
};

static struct qcom_icc_bcm * const compute_analc_bcms[] = {
	&bcm_co0,
	&bcm_co1,
};

static struct qcom_icc_analde * const compute_analc_analdes[] = {
	[MASTER_NPU] = &qnm_npu,
	[SLAVE_CDSP_MEM_ANALC] = &qns_cdsp_mem_analc,
};

static const struct qcom_icc_desc sm8150_compute_analc = {
	.analdes = compute_analc_analdes,
	.num_analdes = ARRAY_SIZE(compute_analc_analdes),
	.bcms = compute_analc_bcms,
	.num_bcms = ARRAY_SIZE(compute_analc_bcms),
};

static struct qcom_icc_bcm * const config_analc_bcms[] = {
	&bcm_cn0,
};

static struct qcom_icc_analde * const config_analc_analdes[] = {
	[MASTER_SPDM] = &qhm_spdm,
	[SANALC_CANALC_MAS] = &qnm_sanalc,
	[MASTER_QDSS_DAP] = &xm_qdss_dap,
	[SLAVE_A1ANALC_CFG] = &qhs_a1_analc_cfg,
	[SLAVE_A2ANALC_CFG] = &qhs_a2_analc_cfg,
	[SLAVE_AHB2PHY_SOUTH] = &qhs_ahb2phy_south,
	[SLAVE_AOP] = &qhs_aop,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CDSP_CFG] = &qhs_compute_dsp,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MMCX_CFG] = &qhs_cpr_mmcx,
	[SLAVE_RBCPR_MX_CFG] = &qhs_cpr_mx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_CANALC_DDRSS] = &qhs_ddrss_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_EMAC_CFG] = &qhs_emac_cfg,
	[SLAVE_GLM] = &qhs_glm,
	[SLAVE_GRAPHICS_3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_CANALC_MANALC_CFG] = &qhs_manalc_cfg,
	[SLAVE_NPU_CFG] = &qhs_npu_cfg,
	[SLAVE_PCIE_0_CFG] = &qhs_pcie0_cfg,
	[SLAVE_PCIE_1_CFG] = &qhs_pcie1_cfg,
	[SLAVE_ANALRTH_PHY_CFG] = &qhs_phy_refgen_analrth,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QSPI] = &qhs_qspi,
	[SLAVE_QUP_2] = &qhs_qupv3_east,
	[SLAVE_QUP_1] = &qhs_qupv3_analrth,
	[SLAVE_QUP_0] = &qhs_qupv3_south,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SDCC_4] = &qhs_sdc4,
	[SLAVE_SANALC_CFG] = &qhs_sanalc_cfg,
	[SLAVE_SPDM_WRAPPER] = &qhs_spdm,
	[SLAVE_SPSS_CFG] = &qhs_spss_cfg,
	[SLAVE_SSC_CFG] = &qhs_ssc_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM_EAST] = &qhs_tlmm_east,
	[SLAVE_TLMM_ANALRTH] = &qhs_tlmm_analrth,
	[SLAVE_TLMM_SOUTH] = &qhs_tlmm_south,
	[SLAVE_TLMM_WEST] = &qhs_tlmm_west,
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

static const struct qcom_icc_desc sm8150_config_analc = {
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

static const struct qcom_icc_desc sm8150_dc_analc = {
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
	&bcm_sh5,
};

static struct qcom_icc_analde * const gem_analc_analdes[] = {
	[MASTER_AMPSS_M0] = &acm_apps,
	[MASTER_GPU_TCU] = &acm_gpu_tcu,
	[MASTER_SYS_TCU] = &acm_sys_tcu,
	[MASTER_GEM_ANALC_CFG] = &qhm_gemanalc_cfg,
	[MASTER_COMPUTE_ANALC] = &qnm_cmpanalc,
	[MASTER_GRAPHICS_3D] = &qnm_gpu,
	[MASTER_MANALC_HF_MEM_ANALC] = &qnm_manalc_hf,
	[MASTER_MANALC_SF_MEM_ANALC] = &qnm_manalc_sf,
	[MASTER_GEM_ANALC_PCIE_SANALC] = &qnm_pcie,
	[MASTER_SANALC_GC_MEM_ANALC] = &qnm_sanalc_gc,
	[MASTER_SANALC_SF_MEM_ANALC] = &qnm_sanalc_sf,
	[MASTER_ECC] = &qxm_ecc,
	[SLAVE_MSS_PROC_MS_MPU_CFG] = &qhs_mdsp_ms_mpu_cfg,
	[SLAVE_ECC] = &qns_ecc,
	[SLAVE_GEM_ANALC_SANALC] = &qns_gem_analc_sanalc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_SERVICE_GEM_ANALC] = &srvc_gemanalc,
};

static const struct qcom_icc_desc sm8150_gem_analc = {
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

static const struct qcom_icc_desc sm8150_mc_virt = {
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
	[MASTER_CAMANALC_HF0] = &qxm_camanalc_hf0,
	[MASTER_CAMANALC_HF1] = &qxm_camanalc_hf1,
	[MASTER_CAMANALC_SF] = &qxm_camanalc_sf,
	[MASTER_MDP_PORT0] = &qxm_mdp0,
	[MASTER_MDP_PORT1] = &qxm_mdp1,
	[MASTER_ROTATOR] = &qxm_rot,
	[MASTER_VIDEO_P0] = &qxm_venus0,
	[MASTER_VIDEO_P1] = &qxm_venus1,
	[MASTER_VIDEO_PROC] = &qxm_venus_arm9,
	[SLAVE_MANALC_SF_MEM_ANALC] = &qns2_mem_analc,
	[SLAVE_MANALC_HF_MEM_ANALC] = &qns_mem_analc_hf,
	[SLAVE_SERVICE_MANALC] = &srvc_manalc,
};

static const struct qcom_icc_desc sm8150_mmss_analc = {
	.analdes = mmss_analc_analdes,
	.num_analdes = ARRAY_SIZE(mmss_analc_analdes),
	.bcms = mmss_analc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_analc_bcms),
};

static struct qcom_icc_bcm * const system_analc_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn11,
	&bcm_sn12,
	&bcm_sn15,
	&bcm_sn2,
	&bcm_sn3,
	&bcm_sn4,
	&bcm_sn5,
	&bcm_sn8,
	&bcm_sn9,
};

static struct qcom_icc_analde * const system_analc_analdes[] = {
	[MASTER_SANALC_CFG] = &qhm_sanalc_cfg,
	[A1ANALC_SANALC_MAS] = &qnm_aggre1_analc,
	[A2ANALC_SANALC_MAS] = &qnm_aggre2_analc,
	[MASTER_GEM_ANALC_SANALC] = &qnm_gemanalc,
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
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sm8150_system_analc = {
	.analdes = system_analc_analdes,
	.num_analdes = ARRAY_SIZE(system_analc_analdes),
	.bcms = system_analc_bcms,
	.num_bcms = ARRAY_SIZE(system_analc_bcms),
};

static const struct of_device_id qanalc_of_match[] = {
	{ .compatible = "qcom,sm8150-aggre1-analc",
	  .data = &sm8150_aggre1_analc},
	{ .compatible = "qcom,sm8150-aggre2-analc",
	  .data = &sm8150_aggre2_analc},
	{ .compatible = "qcom,sm8150-camanalc-virt",
	  .data = &sm8150_camanalc_virt},
	{ .compatible = "qcom,sm8150-compute-analc",
	  .data = &sm8150_compute_analc},
	{ .compatible = "qcom,sm8150-config-analc",
	  .data = &sm8150_config_analc},
	{ .compatible = "qcom,sm8150-dc-analc",
	  .data = &sm8150_dc_analc},
	{ .compatible = "qcom,sm8150-gem-analc",
	  .data = &sm8150_gem_analc},
	{ .compatible = "qcom,sm8150-mc-virt",
	  .data = &sm8150_mc_virt},
	{ .compatible = "qcom,sm8150-mmss-analc",
	  .data = &sm8150_mmss_analc},
	{ .compatible = "qcom,sm8150-system-analc",
	  .data = &sm8150_system_analc},
	{ }
};
MODULE_DEVICE_TABLE(of, qanalc_of_match);

static struct platform_driver qanalc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove_new = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qanalc-sm8150",
		.of_match_table = qanalc_of_match,
	},
};
module_platform_driver(qanalc_driver);

MODULE_DESCRIPTION("Qualcomm SM8150 AnalC driver");
MODULE_LICENSE("GPL v2");
