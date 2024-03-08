// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Linaro Ltd
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <dt-bindings/interconnect/qcom,sc8280xp.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"
#include "sc8280xp.h"

static struct qcom_icc_analde qhm_qspi = {
	.name = "qhm_qspi",
	.id = SC8280XP_MASTER_QSPI_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qup1 = {
	.name = "qhm_qup1",
	.id = SC8280XP_MASTER_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qup2 = {
	.name = "qhm_qup2",
	.id = SC8280XP_MASTER_QUP_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qnm_a1analc_cfg = {
	.name = "qnm_a1analc_cfg",
	.id = SC8280XP_MASTER_A1ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.links = { SC8280XP_SLAVE_SERVICE_A1ANALC },
};

static struct qcom_icc_analde qxm_ipa = {
	.name = "qxm_ipa",
	.id = SC8280XP_MASTER_IPA,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_emac_1 = {
	.name = "xm_emac_1",
	.id = SC8280XP_MASTER_EMAC_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_sdc4 = {
	.name = "xm_sdc4",
	.id = SC8280XP_MASTER_SDCC_4,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.id = SC8280XP_MASTER_UFS_MEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_usb3_0 = {
	.name = "xm_usb3_0",
	.id = SC8280XP_MASTER_USB3_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_USB_ANALC_SANALC },
};

static struct qcom_icc_analde xm_usb3_1 = {
	.name = "xm_usb3_1",
	.id = SC8280XP_MASTER_USB3_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_USB_ANALC_SANALC },
};

static struct qcom_icc_analde xm_usb3_mp = {
	.name = "xm_usb3_mp",
	.id = SC8280XP_MASTER_USB3_MP,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_USB_ANALC_SANALC },
};

static struct qcom_icc_analde xm_usb4_host0 = {
	.name = "xm_usb4_host0",
	.id = SC8280XP_MASTER_USB4_0,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_USB_ANALC_SANALC },
};

static struct qcom_icc_analde xm_usb4_host1 = {
	.name = "xm_usb4_host1",
	.id = SC8280XP_MASTER_USB4_1,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_USB_ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = SC8280XP_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qup0 = {
	.name = "qhm_qup0",
	.id = SC8280XP_MASTER_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qnm_a2analc_cfg = {
	.name = "qnm_a2analc_cfg",
	.id = SC8280XP_MASTER_A2ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_SERVICE_A2ANALC },
};

static struct qcom_icc_analde qxm_crypto = {
	.name = "qxm_crypto",
	.id = SC8280XP_MASTER_CRYPTO,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qxm_sensorss_q6 = {
	.name = "qxm_sensorss_q6",
	.id = SC8280XP_MASTER_SENSORS_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qxm_sp = {
	.name = "qxm_sp",
	.id = SC8280XP_MASTER_SP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde xm_emac_0 = {
	.name = "xm_emac_0",
	.id = SC8280XP_MASTER_EMAC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde xm_pcie3_0 = {
	.name = "xm_pcie3_0",
	.id = SC8280XP_MASTER_PCIE_0,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde xm_pcie3_1 = {
	.name = "xm_pcie3_1",
	.id = SC8280XP_MASTER_PCIE_1,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde xm_pcie3_2a = {
	.name = "xm_pcie3_2a",
	.id = SC8280XP_MASTER_PCIE_2A,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde xm_pcie3_2b = {
	.name = "xm_pcie3_2b",
	.id = SC8280XP_MASTER_PCIE_2B,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde xm_pcie3_3a = {
	.name = "xm_pcie3_3a",
	.id = SC8280XP_MASTER_PCIE_3A,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde xm_pcie3_3b = {
	.name = "xm_pcie3_3b",
	.id = SC8280XP_MASTER_PCIE_3B,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde xm_pcie3_4 = {
	.name = "xm_pcie3_4",
	.id = SC8280XP_MASTER_PCIE_4,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde xm_qdss_etr = {
	.name = "xm_qdss_etr",
	.id = SC8280XP_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde xm_sdc2 = {
	.name = "xm_sdc2",
	.id = SC8280XP_MASTER_SDCC_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde xm_ufs_card = {
	.name = "xm_ufs_card",
	.id = SC8280XP_MASTER_UFS_CARD,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qup0_core_master = {
	.name = "qup0_core_master",
	.id = SC8280XP_MASTER_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_QUP_CORE_0 },
};

static struct qcom_icc_analde qup1_core_master = {
	.name = "qup1_core_master",
	.id = SC8280XP_MASTER_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_QUP_CORE_1 },
};

static struct qcom_icc_analde qup2_core_master = {
	.name = "qup2_core_master",
	.id = SC8280XP_MASTER_QUP_CORE_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_QUP_CORE_2 },
};

static struct qcom_icc_analde qnm_gemanalc_canalc = {
	.name = "qnm_gemanalc_canalc",
	.id = SC8280XP_MASTER_GEM_ANALC_CANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 76,
	.links = { SC8280XP_SLAVE_AHB2PHY_0,
		   SC8280XP_SLAVE_AHB2PHY_1,
		   SC8280XP_SLAVE_AHB2PHY_2,
		   SC8280XP_SLAVE_AOSS,
		   SC8280XP_SLAVE_APPSS,
		   SC8280XP_SLAVE_CAMERA_CFG,
		   SC8280XP_SLAVE_CLK_CTL,
		   SC8280XP_SLAVE_CDSP_CFG,
		   SC8280XP_SLAVE_CDSP1_CFG,
		   SC8280XP_SLAVE_RBCPR_CX_CFG,
		   SC8280XP_SLAVE_RBCPR_MMCX_CFG,
		   SC8280XP_SLAVE_RBCPR_MX_CFG,
		   SC8280XP_SLAVE_CPR_NSPCX,
		   SC8280XP_SLAVE_CRYPTO_0_CFG,
		   SC8280XP_SLAVE_CX_RDPM,
		   SC8280XP_SLAVE_DCC_CFG,
		   SC8280XP_SLAVE_DISPLAY_CFG,
		   SC8280XP_SLAVE_DISPLAY1_CFG,
		   SC8280XP_SLAVE_EMAC_CFG,
		   SC8280XP_SLAVE_EMAC1_CFG,
		   SC8280XP_SLAVE_GFX3D_CFG,
		   SC8280XP_SLAVE_HWKM,
		   SC8280XP_SLAVE_IMEM_CFG,
		   SC8280XP_SLAVE_IPA_CFG,
		   SC8280XP_SLAVE_IPC_ROUTER_CFG,
		   SC8280XP_SLAVE_LPASS,
		   SC8280XP_SLAVE_MX_RDPM,
		   SC8280XP_SLAVE_MXC_RDPM,
		   SC8280XP_SLAVE_PCIE_0_CFG,
		   SC8280XP_SLAVE_PCIE_1_CFG,
		   SC8280XP_SLAVE_PCIE_2A_CFG,
		   SC8280XP_SLAVE_PCIE_2B_CFG,
		   SC8280XP_SLAVE_PCIE_3A_CFG,
		   SC8280XP_SLAVE_PCIE_3B_CFG,
		   SC8280XP_SLAVE_PCIE_4_CFG,
		   SC8280XP_SLAVE_PCIE_RSC_CFG,
		   SC8280XP_SLAVE_PDM,
		   SC8280XP_SLAVE_PIMEM_CFG,
		   SC8280XP_SLAVE_PKA_WRAPPER_CFG,
		   SC8280XP_SLAVE_PMU_WRAPPER_CFG,
		   SC8280XP_SLAVE_QDSS_CFG,
		   SC8280XP_SLAVE_QSPI_0,
		   SC8280XP_SLAVE_QUP_0,
		   SC8280XP_SLAVE_QUP_1,
		   SC8280XP_SLAVE_QUP_2,
		   SC8280XP_SLAVE_SDCC_2,
		   SC8280XP_SLAVE_SDCC_4,
		   SC8280XP_SLAVE_SECURITY,
		   SC8280XP_SLAVE_SMMUV3_CFG,
		   SC8280XP_SLAVE_SMSS_CFG,
		   SC8280XP_SLAVE_SPSS_CFG,
		   SC8280XP_SLAVE_TCSR,
		   SC8280XP_SLAVE_TLMM,
		   SC8280XP_SLAVE_UFS_CARD_CFG,
		   SC8280XP_SLAVE_UFS_MEM_CFG,
		   SC8280XP_SLAVE_USB3_0,
		   SC8280XP_SLAVE_USB3_1,
		   SC8280XP_SLAVE_USB3_MP,
		   SC8280XP_SLAVE_USB4_0,
		   SC8280XP_SLAVE_USB4_1,
		   SC8280XP_SLAVE_VENUS_CFG,
		   SC8280XP_SLAVE_VSENSE_CTRL_CFG,
		   SC8280XP_SLAVE_VSENSE_CTRL_R_CFG,
		   SC8280XP_SLAVE_A1ANALC_CFG,
		   SC8280XP_SLAVE_A2ANALC_CFG,
		   SC8280XP_SLAVE_AANALC_PCIE_BRIDGE_CFG,
		   SC8280XP_SLAVE_DDRSS_CFG,
		   SC8280XP_SLAVE_CANALC_MANALC_CFG,
		   SC8280XP_SLAVE_SANALC_CFG,
		   SC8280XP_SLAVE_SANALC_SF_BRIDGE_CFG,
		   SC8280XP_SLAVE_IMEM,
		   SC8280XP_SLAVE_PIMEM,
		   SC8280XP_SLAVE_SERVICE_CANALC,
		   SC8280XP_SLAVE_QDSS_STM,
		   SC8280XP_SLAVE_SMSS,
		   SC8280XP_SLAVE_TCU
	},
};

static struct qcom_icc_analde qnm_gemanalc_pcie = {
	.name = "qnm_gemanalc_pcie",
	.id = SC8280XP_MASTER_GEM_ANALC_PCIE_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 7,
	.links = { SC8280XP_SLAVE_PCIE_0,
		   SC8280XP_SLAVE_PCIE_1,
		   SC8280XP_SLAVE_PCIE_2A,
		   SC8280XP_SLAVE_PCIE_2B,
		   SC8280XP_SLAVE_PCIE_3A,
		   SC8280XP_SLAVE_PCIE_3B,
		   SC8280XP_SLAVE_PCIE_4
	},
};

static struct qcom_icc_analde qnm_canalc_dc_analc = {
	.name = "qnm_canalc_dc_analc",
	.id = SC8280XP_MASTER_CANALC_DC_ANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.links = { SC8280XP_SLAVE_LLCC_CFG,
		   SC8280XP_SLAVE_GEM_ANALC_CFG
	},
};

static struct qcom_icc_analde alm_gpu_tcu = {
	.name = "alm_gpu_tcu",
	.id = SC8280XP_MASTER_GPU_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SC8280XP_SLAVE_GEM_ANALC_CANALC,
		   SC8280XP_SLAVE_LLCC
	},
};

static struct qcom_icc_analde alm_pcie_tcu = {
	.name = "alm_pcie_tcu",
	.id = SC8280XP_MASTER_PCIE_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SC8280XP_SLAVE_GEM_ANALC_CANALC,
		   SC8280XP_SLAVE_LLCC
	},
};

static struct qcom_icc_analde alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.id = SC8280XP_MASTER_SYS_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SC8280XP_SLAVE_GEM_ANALC_CANALC,
		   SC8280XP_SLAVE_LLCC
	},
};

static struct qcom_icc_analde chm_apps = {
	.name = "chm_apps",
	.id = SC8280XP_MASTER_APPSS_PROC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 3,
	.links = { SC8280XP_SLAVE_GEM_ANALC_CANALC,
		   SC8280XP_SLAVE_LLCC,
		   SC8280XP_SLAVE_GEM_ANALC_PCIE_CANALC
	},
};

static struct qcom_icc_analde qnm_cmpanalc0 = {
	.name = "qnm_cmpanalc0",
	.id = SC8280XP_MASTER_COMPUTE_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SC8280XP_SLAVE_GEM_ANALC_CANALC,
		   SC8280XP_SLAVE_LLCC
	},
};

static struct qcom_icc_analde qnm_cmpanalc1 = {
	.name = "qnm_cmpanalc1",
	.id = SC8280XP_MASTER_COMPUTE_ANALC_1,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SC8280XP_SLAVE_GEM_ANALC_CANALC,
		   SC8280XP_SLAVE_LLCC
	},
};

static struct qcom_icc_analde qnm_gemanalc_cfg = {
	.name = "qnm_gemanalc_cfg",
	.id = SC8280XP_MASTER_GEM_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 3,
	.links = { SC8280XP_SLAVE_SERVICE_GEM_ANALC_1,
		   SC8280XP_SLAVE_SERVICE_GEM_ANALC_2,
		   SC8280XP_SLAVE_SERVICE_GEM_ANALC
	},
};

static struct qcom_icc_analde qnm_gpu = {
	.name = "qnm_gpu",
	.id = SC8280XP_MASTER_GFX3D,
	.channels = 4,
	.buswidth = 32,
	.num_links = 2,
	.links = { SC8280XP_SLAVE_GEM_ANALC_CANALC,
		   SC8280XP_SLAVE_LLCC
	},
};

static struct qcom_icc_analde qnm_manalc_hf = {
	.name = "qnm_manalc_hf",
	.id = SC8280XP_MASTER_MANALC_HF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SC8280XP_SLAVE_LLCC,
		   SC8280XP_SLAVE_GEM_ANALC_PCIE_CANALC
	},
};

static struct qcom_icc_analde qnm_manalc_sf = {
	.name = "qnm_manalc_sf",
	.id = SC8280XP_MASTER_MANALC_SF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SC8280XP_SLAVE_GEM_ANALC_CANALC,
		   SC8280XP_SLAVE_LLCC
	},
};

static struct qcom_icc_analde qnm_pcie = {
	.name = "qnm_pcie",
	.id = SC8280XP_MASTER_AANALC_PCIE_GEM_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 2,
	.links = { SC8280XP_SLAVE_GEM_ANALC_CANALC,
		   SC8280XP_SLAVE_LLCC
	},
};

static struct qcom_icc_analde qnm_sanalc_gc = {
	.name = "qnm_sanalc_gc",
	.id = SC8280XP_MASTER_SANALC_GC_MEM_ANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_sanalc_sf = {
	.name = "qnm_sanalc_sf",
	.id = SC8280XP_MASTER_SANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { SC8280XP_SLAVE_GEM_ANALC_CANALC,
		   SC8280XP_SLAVE_LLCC,
		   SC8280XP_SLAVE_GEM_ANALC_PCIE_CANALC },
};

static struct qcom_icc_analde qhm_config_analc = {
	.name = "qhm_config_analc",
	.id = SC8280XP_MASTER_CANALC_LPASS_AG_ANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 6,
	.links = { SC8280XP_SLAVE_LPASS_CORE_CFG,
		   SC8280XP_SLAVE_LPASS_LPI_CFG,
		   SC8280XP_SLAVE_LPASS_MPU_CFG,
		   SC8280XP_SLAVE_LPASS_TOP_CFG,
		   SC8280XP_SLAVE_SERVICES_LPASS_AML_ANALC,
		   SC8280XP_SLAVE_SERVICE_LPASS_AG_ANALC
	},
};

static struct qcom_icc_analde qxm_lpass_dsp = {
	.name = "qxm_lpass_dsp",
	.id = SC8280XP_MASTER_LPASS_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 4,
	.links = { SC8280XP_SLAVE_LPASS_TOP_CFG,
		   SC8280XP_SLAVE_LPASS_SANALC,
		   SC8280XP_SLAVE_SERVICES_LPASS_AML_ANALC,
		   SC8280XP_SLAVE_SERVICE_LPASS_AG_ANALC
	},
};

static struct qcom_icc_analde llcc_mc = {
	.name = "llcc_mc",
	.id = SC8280XP_MASTER_LLCC,
	.channels = 8,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_EBI1 },
};

static struct qcom_icc_analde qnm_camanalc_hf = {
	.name = "qnm_camanalc_hf",
	.id = SC8280XP_MASTER_CAMANALC_HF,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_mdp0_0 = {
	.name = "qnm_mdp0_0",
	.id = SC8280XP_MASTER_MDP0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_mdp0_1 = {
	.name = "qnm_mdp0_1",
	.id = SC8280XP_MASTER_MDP1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_mdp1_0 = {
	.name = "qnm_mdp1_0",
	.id = SC8280XP_MASTER_MDP_CORE1_0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_mdp1_1 = {
	.name = "qnm_mdp1_1",
	.id = SC8280XP_MASTER_MDP_CORE1_1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_manalc_cfg = {
	.name = "qnm_manalc_cfg",
	.id = SC8280XP_MASTER_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_SERVICE_MANALC },
};

static struct qcom_icc_analde qnm_rot_0 = {
	.name = "qnm_rot_0",
	.id = SC8280XP_MASTER_ROTATOR,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_rot_1 = {
	.name = "qnm_rot_1",
	.id = SC8280XP_MASTER_ROTATOR_1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_video0 = {
	.name = "qnm_video0",
	.id = SC8280XP_MASTER_VIDEO_P0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_video1 = {
	.name = "qnm_video1",
	.id = SC8280XP_MASTER_VIDEO_P1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_video_cvp = {
	.name = "qnm_video_cvp",
	.id = SC8280XP_MASTER_VIDEO_PROC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_camanalc_icp = {
	.name = "qxm_camanalc_icp",
	.id = SC8280XP_MASTER_CAMANALC_ICP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_camanalc_sf = {
	.name = "qxm_camanalc_sf",
	.id = SC8280XP_MASTER_CAMANALC_SF,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qhm_nsp_analc_config = {
	.name = "qhm_nsp_analc_config",
	.id = SC8280XP_MASTER_CDSP_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_SERVICE_NSP_ANALC },
};

static struct qcom_icc_analde qxm_nsp = {
	.name = "qxm_nsp",
	.id = SC8280XP_MASTER_CDSP_PROC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SC8280XP_SLAVE_CDSP_MEM_ANALC,
		   SC8280XP_SLAVE_NSP_XFR
	},
};

static struct qcom_icc_analde qhm_nspb_analc_config = {
	.name = "qhm_nspb_analc_config",
	.id = SC8280XP_MASTER_CDSPB_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_SERVICE_NSPB_ANALC },
};

static struct qcom_icc_analde qxm_nspb = {
	.name = "qxm_nspb",
	.id = SC8280XP_MASTER_CDSP_PROC_B,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SC8280XP_SLAVE_CDSPB_MEM_ANALC,
		   SC8280XP_SLAVE_NSPB_XFR
	},
};

static struct qcom_icc_analde qnm_aggre1_analc = {
	.name = "qnm_aggre1_analc",
	.id = SC8280XP_MASTER_A1ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qnm_aggre2_analc = {
	.name = "qnm_aggre2_analc",
	.id = SC8280XP_MASTER_A2ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qnm_aggre_usb_analc = {
	.name = "qnm_aggre_usb_analc",
	.id = SC8280XP_MASTER_USB_ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qnm_lpass_analc = {
	.name = "qnm_lpass_analc",
	.id = SC8280XP_MASTER_LPASS_AANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qnm_sanalc_cfg = {
	.name = "qnm_sanalc_cfg",
	.id = SC8280XP_MASTER_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_SERVICE_SANALC },
};

static struct qcom_icc_analde qxm_pimem = {
	.name = "qxm_pimem",
	.id = SC8280XP_MASTER_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_SANALC_GEM_ANALC_GC },
};

static struct qcom_icc_analde xm_gic = {
	.name = "xm_gic",
	.id = SC8280XP_MASTER_GIC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8280XP_SLAVE_SANALC_GEM_ANALC_GC },
};

static struct qcom_icc_analde qns_a1analc_sanalc = {
	.name = "qns_a1analc_sanalc",
	.id = SC8280XP_SLAVE_A1ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8280XP_MASTER_A1ANALC_SANALC },
};

static struct qcom_icc_analde qns_aggre_usb_sanalc = {
	.name = "qns_aggre_usb_sanalc",
	.id = SC8280XP_SLAVE_USB_ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8280XP_MASTER_USB_ANALC_SANALC },
};

static struct qcom_icc_analde srvc_aggre1_analc = {
	.name = "srvc_aggre1_analc",
	.id = SC8280XP_SLAVE_SERVICE_A1ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_a2analc_sanalc = {
	.name = "qns_a2analc_sanalc",
	.id = SC8280XP_SLAVE_A2ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8280XP_MASTER_A2ANALC_SANALC },
};

static struct qcom_icc_analde qns_pcie_gem_analc = {
	.name = "qns_pcie_gem_analc",
	.id = SC8280XP_SLAVE_AANALC_PCIE_GEM_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8280XP_MASTER_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde srvc_aggre2_analc = {
	.name = "srvc_aggre2_analc",
	.id = SC8280XP_SLAVE_SERVICE_A2ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qup0_core_slave = {
	.name = "qup0_core_slave",
	.id = SC8280XP_SLAVE_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qup1_core_slave = {
	.name = "qup1_core_slave",
	.id = SC8280XP_SLAVE_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qup2_core_slave = {
	.name = "qup2_core_slave",
	.id = SC8280XP_SLAVE_QUP_CORE_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ahb2phy0 = {
	.name = "qhs_ahb2phy0",
	.id = SC8280XP_SLAVE_AHB2PHY_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ahb2phy1 = {
	.name = "qhs_ahb2phy1",
	.id = SC8280XP_SLAVE_AHB2PHY_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ahb2phy2 = {
	.name = "qhs_ahb2phy2",
	.id = SC8280XP_SLAVE_AHB2PHY_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_aoss = {
	.name = "qhs_aoss",
	.id = SC8280XP_SLAVE_AOSS,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_apss = {
	.name = "qhs_apss",
	.id = SC8280XP_SLAVE_APPSS,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qhs_camera_cfg = {
	.name = "qhs_camera_cfg",
	.id = SC8280XP_SLAVE_CAMERA_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SC8280XP_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_compute0_cfg = {
	.name = "qhs_compute0_cfg",
	.id = SC8280XP_SLAVE_CDSP_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_MASTER_CDSP_ANALC_CFG },
};

static struct qcom_icc_analde qhs_compute1_cfg = {
	.name = "qhs_compute1_cfg",
	.id = SC8280XP_SLAVE_CDSP1_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_MASTER_CDSPB_ANALC_CFG },
};

static struct qcom_icc_analde qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = SC8280XP_SLAVE_RBCPR_CX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cpr_mmcx = {
	.name = "qhs_cpr_mmcx",
	.id = SC8280XP_SLAVE_RBCPR_MMCX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cpr_mx = {
	.name = "qhs_cpr_mx",
	.id = SC8280XP_SLAVE_RBCPR_MX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cpr_nspcx = {
	.name = "qhs_cpr_nspcx",
	.id = SC8280XP_SLAVE_CPR_NSPCX,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = SC8280XP_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cx_rdpm = {
	.name = "qhs_cx_rdpm",
	.id = SC8280XP_SLAVE_CX_RDPM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_dcc_cfg = {
	.name = "qhs_dcc_cfg",
	.id = SC8280XP_SLAVE_DCC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_display0_cfg = {
	.name = "qhs_display0_cfg",
	.id = SC8280XP_SLAVE_DISPLAY_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_display1_cfg = {
	.name = "qhs_display1_cfg",
	.id = SC8280XP_SLAVE_DISPLAY1_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_emac0_cfg = {
	.name = "qhs_emac0_cfg",
	.id = SC8280XP_SLAVE_EMAC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_emac1_cfg = {
	.name = "qhs_emac1_cfg",
	.id = SC8280XP_SLAVE_EMAC1_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.id = SC8280XP_SLAVE_GFX3D_CFG,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qhs_hwkm = {
	.name = "qhs_hwkm",
	.id = SC8280XP_SLAVE_HWKM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SC8280XP_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ipa = {
	.name = "qhs_ipa",
	.id = SC8280XP_SLAVE_IPA_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ipc_router = {
	.name = "qhs_ipc_router",
	.id = SC8280XP_SLAVE_IPC_ROUTER_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_lpass_cfg = {
	.name = "qhs_lpass_cfg",
	.id = SC8280XP_SLAVE_LPASS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_MASTER_CANALC_LPASS_AG_ANALC },
};

static struct qcom_icc_analde qhs_mx_rdpm = {
	.name = "qhs_mx_rdpm",
	.id = SC8280XP_SLAVE_MX_RDPM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_mxc_rdpm = {
	.name = "qhs_mxc_rdpm",
	.id = SC8280XP_SLAVE_MXC_RDPM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pcie0_cfg = {
	.name = "qhs_pcie0_cfg",
	.id = SC8280XP_SLAVE_PCIE_0_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pcie1_cfg = {
	.name = "qhs_pcie1_cfg",
	.id = SC8280XP_SLAVE_PCIE_1_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pcie2a_cfg = {
	.name = "qhs_pcie2a_cfg",
	.id = SC8280XP_SLAVE_PCIE_2A_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pcie2b_cfg = {
	.name = "qhs_pcie2b_cfg",
	.id = SC8280XP_SLAVE_PCIE_2B_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pcie3a_cfg = {
	.name = "qhs_pcie3a_cfg",
	.id = SC8280XP_SLAVE_PCIE_3A_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pcie3b_cfg = {
	.name = "qhs_pcie3b_cfg",
	.id = SC8280XP_SLAVE_PCIE_3B_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pcie4_cfg = {
	.name = "qhs_pcie4_cfg",
	.id = SC8280XP_SLAVE_PCIE_4_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pcie_rsc_cfg = {
	.name = "qhs_pcie_rsc_cfg",
	.id = SC8280XP_SLAVE_PCIE_RSC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pdm = {
	.name = "qhs_pdm",
	.id = SC8280XP_SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = SC8280XP_SLAVE_PIMEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pka_wrapper_cfg = {
	.name = "qhs_pka_wrapper_cfg",
	.id = SC8280XP_SLAVE_PKA_WRAPPER_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pmu_wrapper_cfg = {
	.name = "qhs_pmu_wrapper_cfg",
	.id = SC8280XP_SLAVE_PMU_WRAPPER_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SC8280XP_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qspi = {
	.name = "qhs_qspi",
	.id = SC8280XP_SLAVE_QSPI_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qup0 = {
	.name = "qhs_qup0",
	.id = SC8280XP_SLAVE_QUP_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qup1 = {
	.name = "qhs_qup1",
	.id = SC8280XP_SLAVE_QUP_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qup2 = {
	.name = "qhs_qup2",
	.id = SC8280XP_SLAVE_QUP_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = SC8280XP_SLAVE_SDCC_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_sdc4 = {
	.name = "qhs_sdc4",
	.id = SC8280XP_SLAVE_SDCC_4,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_security = {
	.name = "qhs_security",
	.id = SC8280XP_SLAVE_SECURITY,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_smmuv3_cfg = {
	.name = "qhs_smmuv3_cfg",
	.id = SC8280XP_SLAVE_SMMUV3_CFG,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qhs_smss_cfg = {
	.name = "qhs_smss_cfg",
	.id = SC8280XP_SLAVE_SMSS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_spss_cfg = {
	.name = "qhs_spss_cfg",
	.id = SC8280XP_SLAVE_SPSS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SC8280XP_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tlmm = {
	.name = "qhs_tlmm",
	.id = SC8280XP_SLAVE_TLMM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ufs_card_cfg = {
	.name = "qhs_ufs_card_cfg",
	.id = SC8280XP_SLAVE_UFS_CARD_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.id = SC8280XP_SLAVE_UFS_MEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_usb3_0 = {
	.name = "qhs_usb3_0",
	.id = SC8280XP_SLAVE_USB3_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_usb3_1 = {
	.name = "qhs_usb3_1",
	.id = SC8280XP_SLAVE_USB3_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_usb3_mp = {
	.name = "qhs_usb3_mp",
	.id = SC8280XP_SLAVE_USB3_MP,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_usb4_host_0 = {
	.name = "qhs_usb4_host_0",
	.id = SC8280XP_SLAVE_USB4_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_usb4_host_1 = {
	.name = "qhs_usb4_host_1",
	.id = SC8280XP_SLAVE_USB4_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = SC8280XP_SLAVE_VENUS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.id = SC8280XP_SLAVE_VSENSE_CTRL_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_vsense_ctrl_r_cfg = {
	.name = "qhs_vsense_ctrl_r_cfg",
	.id = SC8280XP_SLAVE_VSENSE_CTRL_R_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_a1_analc_cfg = {
	.name = "qns_a1_analc_cfg",
	.id = SC8280XP_SLAVE_A1ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_MASTER_A1ANALC_CFG },
};

static struct qcom_icc_analde qns_a2_analc_cfg = {
	.name = "qns_a2_analc_cfg",
	.id = SC8280XP_SLAVE_A2ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_MASTER_A2ANALC_CFG },
};

static struct qcom_icc_analde qns_aanalc_pcie_bridge_cfg = {
	.name = "qns_aanalc_pcie_bridge_cfg",
	.id = SC8280XP_SLAVE_AANALC_PCIE_BRIDGE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_ddrss_cfg = {
	.name = "qns_ddrss_cfg",
	.id = SC8280XP_SLAVE_DDRSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_MASTER_CANALC_DC_ANALC },
};

static struct qcom_icc_analde qns_manalc_cfg = {
	.name = "qns_manalc_cfg",
	.id = SC8280XP_SLAVE_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_MASTER_CANALC_MANALC_CFG },
};

static struct qcom_icc_analde qns_sanalc_cfg = {
	.name = "qns_sanalc_cfg",
	.id = SC8280XP_SLAVE_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_MASTER_SANALC_CFG },
};

static struct qcom_icc_analde qns_sanalc_sf_bridge_cfg = {
	.name = "qns_sanalc_sf_bridge_cfg",
	.id = SC8280XP_SLAVE_SANALC_SF_BRIDGE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qxs_imem = {
	.name = "qxs_imem",
	.id = SC8280XP_SLAVE_IMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qxs_pimem = {
	.name = "qxs_pimem",
	.id = SC8280XP_SLAVE_PIMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde srvc_canalc = {
	.name = "srvc_canalc",
	.id = SC8280XP_SLAVE_SERVICE_CANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde xs_pcie_0 = {
	.name = "xs_pcie_0",
	.id = SC8280XP_SLAVE_PCIE_0,
	.channels = 1,
	.buswidth = 16,
};

static struct qcom_icc_analde xs_pcie_1 = {
	.name = "xs_pcie_1",
	.id = SC8280XP_SLAVE_PCIE_1,
	.channels = 1,
	.buswidth = 16,
};

static struct qcom_icc_analde xs_pcie_2a = {
	.name = "xs_pcie_2a",
	.id = SC8280XP_SLAVE_PCIE_2A,
	.channels = 1,
	.buswidth = 16,
};

static struct qcom_icc_analde xs_pcie_2b = {
	.name = "xs_pcie_2b",
	.id = SC8280XP_SLAVE_PCIE_2B,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde xs_pcie_3a = {
	.name = "xs_pcie_3a",
	.id = SC8280XP_SLAVE_PCIE_3A,
	.channels = 1,
	.buswidth = 16,
};

static struct qcom_icc_analde xs_pcie_3b = {
	.name = "xs_pcie_3b",
	.id = SC8280XP_SLAVE_PCIE_3B,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde xs_pcie_4 = {
	.name = "xs_pcie_4",
	.id = SC8280XP_SLAVE_PCIE_4,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SC8280XP_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde xs_smss = {
	.name = "xs_smss",
	.id = SC8280XP_SLAVE_SMSS,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SC8280XP_SLAVE_TCU,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qhs_llcc = {
	.name = "qhs_llcc",
	.id = SC8280XP_SLAVE_LLCC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_gemanalc = {
	.name = "qns_gemanalc",
	.id = SC8280XP_SLAVE_GEM_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC8280XP_MASTER_GEM_ANALC_CFG },
};

static struct qcom_icc_analde qns_gem_analc_canalc = {
	.name = "qns_gem_analc_canalc",
	.id = SC8280XP_SLAVE_GEM_ANALC_CANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8280XP_MASTER_GEM_ANALC_CANALC },
};

static struct qcom_icc_analde qns_llcc = {
	.name = "qns_llcc",
	.id = SC8280XP_SLAVE_LLCC,
	.channels = 8,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8280XP_MASTER_LLCC },
};

static struct qcom_icc_analde qns_pcie = {
	.name = "qns_pcie",
	.id = SC8280XP_SLAVE_GEM_ANALC_PCIE_CANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8280XP_MASTER_GEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde srvc_even_gemanalc = {
	.name = "srvc_even_gemanalc",
	.id = SC8280XP_SLAVE_SERVICE_GEM_ANALC_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde srvc_odd_gemanalc = {
	.name = "srvc_odd_gemanalc",
	.id = SC8280XP_SLAVE_SERVICE_GEM_ANALC_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde srvc_sys_gemanalc = {
	.name = "srvc_sys_gemanalc",
	.id = SC8280XP_SLAVE_SERVICE_GEM_ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_lpass_core = {
	.name = "qhs_lpass_core",
	.id = SC8280XP_SLAVE_LPASS_CORE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_lpass_lpi = {
	.name = "qhs_lpass_lpi",
	.id = SC8280XP_SLAVE_LPASS_LPI_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_lpass_mpu = {
	.name = "qhs_lpass_mpu",
	.id = SC8280XP_SLAVE_LPASS_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_lpass_top = {
	.name = "qhs_lpass_top",
	.id = SC8280XP_SLAVE_LPASS_TOP_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_sysanalc = {
	.name = "qns_sysanalc",
	.id = SC8280XP_SLAVE_LPASS_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8280XP_MASTER_LPASS_AANALC },
};

static struct qcom_icc_analde srvc_niu_aml_analc = {
	.name = "srvc_niu_aml_analc",
	.id = SC8280XP_SLAVE_SERVICES_LPASS_AML_ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde srvc_niu_lpass_aganalc = {
	.name = "srvc_niu_lpass_aganalc",
	.id = SC8280XP_SLAVE_SERVICE_LPASS_AG_ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde ebi = {
	.name = "ebi",
	.id = SC8280XP_SLAVE_EBI1,
	.channels = 8,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_mem_analc_hf = {
	.name = "qns_mem_analc_hf",
	.id = SC8280XP_SLAVE_MANALC_HF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8280XP_MASTER_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qns_mem_analc_sf = {
	.name = "qns_mem_analc_sf",
	.id = SC8280XP_SLAVE_MANALC_SF_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8280XP_MASTER_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde srvc_manalc = {
	.name = "srvc_manalc",
	.id = SC8280XP_SLAVE_SERVICE_MANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_nsp_gemanalc = {
	.name = "qns_nsp_gemanalc",
	.id = SC8280XP_SLAVE_CDSP_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8280XP_MASTER_COMPUTE_ANALC },
};

static struct qcom_icc_analde qxs_nsp_xfr = {
	.name = "qxs_nsp_xfr",
	.id = SC8280XP_SLAVE_NSP_XFR,
	.channels = 1,
	.buswidth = 32,
};

static struct qcom_icc_analde service_nsp_analc = {
	.name = "service_nsp_analc",
	.id = SC8280XP_SLAVE_SERVICE_NSP_ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_nspb_gemanalc = {
	.name = "qns_nspb_gemanalc",
	.id = SC8280XP_SLAVE_CDSPB_MEM_ANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC8280XP_MASTER_COMPUTE_ANALC_1 },
};

static struct qcom_icc_analde qxs_nspb_xfr = {
	.name = "qxs_nspb_xfr",
	.id = SC8280XP_SLAVE_NSPB_XFR,
	.channels = 1,
	.buswidth = 32,
};

static struct qcom_icc_analde service_nspb_analc = {
	.name = "service_nspb_analc",
	.id = SC8280XP_SLAVE_SERVICE_NSPB_ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_gemanalc_gc = {
	.name = "qns_gemanalc_gc",
	.id = SC8280XP_SLAVE_SANALC_GEM_ANALC_GC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC8280XP_MASTER_SANALC_GC_MEM_ANALC },
};

static struct qcom_icc_analde qns_gemanalc_sf = {
	.name = "qns_gemanalc_sf",
	.id = SC8280XP_SLAVE_SANALC_GEM_ANALC_SF,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC8280XP_MASTER_SANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde srvc_sanalc = {
	.name = "srvc_sanalc",
	.id = SC8280XP_SLAVE_SERVICE_SANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
	.enable_mask = BIT(3),
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
	.keepalive = true,
	.num_analdes = 9,
	.analdes = { &qnm_gemanalc_canalc,
		   &qnm_gemanalc_pcie,
		   &xs_pcie_0,
		   &xs_pcie_1,
		   &xs_pcie_2a,
		   &xs_pcie_2b,
		   &xs_pcie_3a,
		   &xs_pcie_3b,
		   &xs_pcie_4
	},
};

static struct qcom_icc_bcm bcm_cn1 = {
	.name = "CN1",
	.num_analdes = 67,
	.analdes = { &qhs_ahb2phy0,
		   &qhs_ahb2phy1,
		   &qhs_ahb2phy2,
		   &qhs_aoss,
		   &qhs_apss,
		   &qhs_camera_cfg,
		   &qhs_clk_ctl,
		   &qhs_compute0_cfg,
		   &qhs_compute1_cfg,
		   &qhs_cpr_cx,
		   &qhs_cpr_mmcx,
		   &qhs_cpr_mx,
		   &qhs_cpr_nspcx,
		   &qhs_crypto0_cfg,
		   &qhs_cx_rdpm,
		   &qhs_dcc_cfg,
		   &qhs_display0_cfg,
		   &qhs_display1_cfg,
		   &qhs_emac0_cfg,
		   &qhs_emac1_cfg,
		   &qhs_gpuss_cfg,
		   &qhs_hwkm,
		   &qhs_imem_cfg,
		   &qhs_ipa,
		   &qhs_ipc_router,
		   &qhs_lpass_cfg,
		   &qhs_mx_rdpm,
		   &qhs_mxc_rdpm,
		   &qhs_pcie0_cfg,
		   &qhs_pcie1_cfg,
		   &qhs_pcie2a_cfg,
		   &qhs_pcie2b_cfg,
		   &qhs_pcie3a_cfg,
		   &qhs_pcie3b_cfg,
		   &qhs_pcie4_cfg,
		   &qhs_pcie_rsc_cfg,
		   &qhs_pdm,
		   &qhs_pimem_cfg,
		   &qhs_pka_wrapper_cfg,
		   &qhs_pmu_wrapper_cfg,
		   &qhs_qdss_cfg,
		   &qhs_sdc2,
		   &qhs_sdc4,
		   &qhs_security,
		   &qhs_smmuv3_cfg,
		   &qhs_smss_cfg,
		   &qhs_spss_cfg,
		   &qhs_tcsr,
		   &qhs_tlmm,
		   &qhs_ufs_card_cfg,
		   &qhs_ufs_mem_cfg,
		   &qhs_usb3_0,
		   &qhs_usb3_1,
		   &qhs_usb3_mp,
		   &qhs_usb4_host_0,
		   &qhs_usb4_host_1,
		   &qhs_venus_cfg,
		   &qhs_vsense_ctrl_cfg,
		   &qhs_vsense_ctrl_r_cfg,
		   &qns_a1_analc_cfg,
		   &qns_a2_analc_cfg,
		   &qns_aanalc_pcie_bridge_cfg,
		   &qns_ddrss_cfg,
		   &qns_manalc_cfg,
		   &qns_sanalc_cfg,
		   &qns_sanalc_sf_bridge_cfg,
		   &srvc_canalc
	},
};

static struct qcom_icc_bcm bcm_cn2 = {
	.name = "CN2",
	.num_analdes = 4,
	.analdes = { &qhs_qspi,
		   &qhs_qup0,
		   &qhs_qup1,
		   &qhs_qup2
	},
};

static struct qcom_icc_bcm bcm_cn3 = {
	.name = "CN3",
	.num_analdes = 3,
	.analdes = { &qxs_imem,
		   &xs_smss,
		   &xs_sys_tcu_cfg
	},
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
	.num_analdes = 5,
	.analdes = { &qnm_camanalc_hf,
		   &qnm_mdp0_0,
		   &qnm_mdp0_1,
		   &qnm_mdp1_0,
		   &qns_mem_analc_hf
	},
};

static struct qcom_icc_bcm bcm_mm1 = {
	.name = "MM1",
	.num_analdes = 8,
	.analdes = { &qnm_rot_0,
		   &qnm_rot_1,
		   &qnm_video0,
		   &qnm_video1,
		   &qnm_video_cvp,
		   &qxm_camanalc_icp,
		   &qxm_camanalc_sf,
		   &qns_mem_analc_sf
	},
};

static struct qcom_icc_bcm bcm_nsa0 = {
	.name = "NSA0",
	.num_analdes = 2,
	.analdes = { &qns_nsp_gemanalc,
		   &qxs_nsp_xfr
	},
};

static struct qcom_icc_bcm bcm_nsa1 = {
	.name = "NSA1",
	.num_analdes = 1,
	.analdes = { &qxm_nsp },
};

static struct qcom_icc_bcm bcm_nsb0 = {
	.name = "NSB0",
	.num_analdes = 2,
	.analdes = { &qns_nspb_gemanalc,
		   &qxs_nspb_xfr
	},
};

static struct qcom_icc_bcm bcm_nsb1 = {
	.name = "NSB1",
	.num_analdes = 1,
	.analdes = { &qxm_nspb },
};

static struct qcom_icc_bcm bcm_pci0 = {
	.name = "PCI0",
	.num_analdes = 1,
	.analdes = { &qns_pcie_gem_analc },
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.vote_scale = 1,
	.num_analdes = 1,
	.analdes = { &qup0_core_slave },
};

static struct qcom_icc_bcm bcm_qup1 = {
	.name = "QUP1",
	.vote_scale = 1,
	.num_analdes = 1,
	.analdes = { &qup1_core_slave },
};

static struct qcom_icc_bcm bcm_qup2 = {
	.name = "QUP2",
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

static struct qcom_icc_bcm bcm_sh2 = {
	.name = "SH2",
	.num_analdes = 1,
	.analdes = { &chm_apps },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &qns_gemanalc_sf },
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.num_analdes = 1,
	.analdes = { &qns_gemanalc_gc },
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.num_analdes = 1,
	.analdes = { &qxs_pimem },
};

static struct qcom_icc_bcm bcm_sn3 = {
	.name = "SN3",
	.num_analdes = 2,
	.analdes = { &qns_a1analc_sanalc,
		   &qnm_aggre1_analc
	},
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.num_analdes = 2,
	.analdes = { &qns_a2analc_sanalc,
		   &qnm_aggre2_analc
	},
};

static struct qcom_icc_bcm bcm_sn5 = {
	.name = "SN5",
	.num_analdes = 2,
	.analdes = { &qns_aggre_usb_sanalc,
		   &qnm_aggre_usb_analc
	},
};

static struct qcom_icc_bcm bcm_sn9 = {
	.name = "SN9",
	.num_analdes = 2,
	.analdes = { &qns_sysanalc,
		   &qnm_lpass_analc
	},
};

static struct qcom_icc_bcm bcm_sn10 = {
	.name = "SN10",
	.num_analdes = 1,
	.analdes = { &xs_qdss_stm },
};

static struct qcom_icc_bcm * const aggre1_analc_bcms[] = {
	&bcm_sn3,
	&bcm_sn5,
};

static struct qcom_icc_analde * const aggre1_analc_analdes[] = {
	[MASTER_QSPI_0] = &qhm_qspi,
	[MASTER_QUP_1] = &qhm_qup1,
	[MASTER_QUP_2] = &qhm_qup2,
	[MASTER_A1ANALC_CFG] = &qnm_a1analc_cfg,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_EMAC_1] = &xm_emac_1,
	[MASTER_SDCC_4] = &xm_sdc4,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_USB3_0] = &xm_usb3_0,
	[MASTER_USB3_1] = &xm_usb3_1,
	[MASTER_USB3_MP] = &xm_usb3_mp,
	[MASTER_USB4_0] = &xm_usb4_host0,
	[MASTER_USB4_1] = &xm_usb4_host1,
	[SLAVE_A1ANALC_SANALC] = &qns_a1analc_sanalc,
	[SLAVE_USB_ANALC_SANALC] = &qns_aggre_usb_sanalc,
	[SLAVE_SERVICE_A1ANALC] = &srvc_aggre1_analc,
};

static const struct qcom_icc_desc sc8280xp_aggre1_analc = {
	.analdes = aggre1_analc_analdes,
	.num_analdes = ARRAY_SIZE(aggre1_analc_analdes),
	.bcms = aggre1_analc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_analc_bcms),
};

static struct qcom_icc_bcm * const aggre2_analc_bcms[] = {
	&bcm_ce0,
	&bcm_pci0,
	&bcm_sn4,
};

static struct qcom_icc_analde * const aggre2_analc_analdes[] = {
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_A2ANALC_CFG] = &qnm_a2analc_cfg,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_SENSORS_PROC] = &qxm_sensorss_q6,
	[MASTER_SP] = &qxm_sp,
	[MASTER_EMAC] = &xm_emac_0,
	[MASTER_PCIE_0] = &xm_pcie3_0,
	[MASTER_PCIE_1] = &xm_pcie3_1,
	[MASTER_PCIE_2A] = &xm_pcie3_2a,
	[MASTER_PCIE_2B] = &xm_pcie3_2b,
	[MASTER_PCIE_3A] = &xm_pcie3_3a,
	[MASTER_PCIE_3B] = &xm_pcie3_3b,
	[MASTER_PCIE_4] = &xm_pcie3_4,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_UFS_CARD] = &xm_ufs_card,
	[SLAVE_A2ANALC_SANALC] = &qns_a2analc_sanalc,
	[SLAVE_AANALC_PCIE_GEM_ANALC] = &qns_pcie_gem_analc,
	[SLAVE_SERVICE_A2ANALC] = &srvc_aggre2_analc,
};

static const struct qcom_icc_desc sc8280xp_aggre2_analc = {
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

static const struct qcom_icc_desc sc8280xp_clk_virt = {
	.analdes = clk_virt_analdes,
	.num_analdes = ARRAY_SIZE(clk_virt_analdes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
};

static struct qcom_icc_bcm * const config_analc_bcms[] = {
	&bcm_cn0,
	&bcm_cn1,
	&bcm_cn2,
	&bcm_cn3,
	&bcm_sn2,
	&bcm_sn10,
};

static struct qcom_icc_analde * const config_analc_analdes[] = {
	[MASTER_GEM_ANALC_CANALC] = &qnm_gemanalc_canalc,
	[MASTER_GEM_ANALC_PCIE_SANALC] = &qnm_gemanalc_pcie,
	[SLAVE_AHB2PHY_0] = &qhs_ahb2phy0,
	[SLAVE_AHB2PHY_1] = &qhs_ahb2phy1,
	[SLAVE_AHB2PHY_2] = &qhs_ahb2phy2,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_APPSS] = &qhs_apss,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CDSP_CFG] = &qhs_compute0_cfg,
	[SLAVE_CDSP1_CFG] = &qhs_compute1_cfg,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MMCX_CFG] = &qhs_cpr_mmcx,
	[SLAVE_RBCPR_MX_CFG] = &qhs_cpr_mx,
	[SLAVE_CPR_NSPCX] = &qhs_cpr_nspcx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_CX_RDPM] = &qhs_cx_rdpm,
	[SLAVE_DCC_CFG] = &qhs_dcc_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_display0_cfg,
	[SLAVE_DISPLAY1_CFG] = &qhs_display1_cfg,
	[SLAVE_EMAC_CFG] = &qhs_emac0_cfg,
	[SLAVE_EMAC1_CFG] = &qhs_emac1_cfg,
	[SLAVE_GFX3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_HWKM] = &qhs_hwkm,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_IPC_ROUTER_CFG] = &qhs_ipc_router,
	[SLAVE_LPASS] = &qhs_lpass_cfg,
	[SLAVE_MX_RDPM] = &qhs_mx_rdpm,
	[SLAVE_MXC_RDPM] = &qhs_mxc_rdpm,
	[SLAVE_PCIE_0_CFG] = &qhs_pcie0_cfg,
	[SLAVE_PCIE_1_CFG] = &qhs_pcie1_cfg,
	[SLAVE_PCIE_2A_CFG] = &qhs_pcie2a_cfg,
	[SLAVE_PCIE_2B_CFG] = &qhs_pcie2b_cfg,
	[SLAVE_PCIE_3A_CFG] = &qhs_pcie3a_cfg,
	[SLAVE_PCIE_3B_CFG] = &qhs_pcie3b_cfg,
	[SLAVE_PCIE_4_CFG] = &qhs_pcie4_cfg,
	[SLAVE_PCIE_RSC_CFG] = &qhs_pcie_rsc_cfg,
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
	[SLAVE_SMMUV3_CFG] = &qhs_smmuv3_cfg,
	[SLAVE_SMSS_CFG] = &qhs_smss_cfg,
	[SLAVE_SPSS_CFG] = &qhs_spss_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_UFS_CARD_CFG] = &qhs_ufs_card_cfg,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB3_0] = &qhs_usb3_0,
	[SLAVE_USB3_1] = &qhs_usb3_1,
	[SLAVE_USB3_MP] = &qhs_usb3_mp,
	[SLAVE_USB4_0] = &qhs_usb4_host_0,
	[SLAVE_USB4_1] = &qhs_usb4_host_1,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_VSENSE_CTRL_R_CFG] = &qhs_vsense_ctrl_r_cfg,
	[SLAVE_A1ANALC_CFG] = &qns_a1_analc_cfg,
	[SLAVE_A2ANALC_CFG] = &qns_a2_analc_cfg,
	[SLAVE_AANALC_PCIE_BRIDGE_CFG] = &qns_aanalc_pcie_bridge_cfg,
	[SLAVE_DDRSS_CFG] = &qns_ddrss_cfg,
	[SLAVE_CANALC_MANALC_CFG] = &qns_manalc_cfg,
	[SLAVE_SANALC_CFG] = &qns_sanalc_cfg,
	[SLAVE_SANALC_SF_BRIDGE_CFG] = &qns_sanalc_sf_bridge_cfg,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SERVICE_CANALC] = &srvc_canalc,
	[SLAVE_PCIE_0] = &xs_pcie_0,
	[SLAVE_PCIE_1] = &xs_pcie_1,
	[SLAVE_PCIE_2A] = &xs_pcie_2a,
	[SLAVE_PCIE_2B] = &xs_pcie_2b,
	[SLAVE_PCIE_3A] = &xs_pcie_3a,
	[SLAVE_PCIE_3B] = &xs_pcie_3b,
	[SLAVE_PCIE_4] = &xs_pcie_4,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_SMSS] = &xs_smss,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sc8280xp_config_analc = {
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

static const struct qcom_icc_desc sc8280xp_dc_analc = {
	.analdes = dc_analc_analdes,
	.num_analdes = ARRAY_SIZE(dc_analc_analdes),
	.bcms = dc_analc_bcms,
	.num_bcms = ARRAY_SIZE(dc_analc_bcms),
};

static struct qcom_icc_bcm * const gem_analc_bcms[] = {
	&bcm_sh0,
	&bcm_sh2,
};

static struct qcom_icc_analde * const gem_analc_analdes[] = {
	[MASTER_GPU_TCU] = &alm_gpu_tcu,
	[MASTER_PCIE_TCU] = &alm_pcie_tcu,
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_APPSS_PROC] = &chm_apps,
	[MASTER_COMPUTE_ANALC] = &qnm_cmpanalc0,
	[MASTER_COMPUTE_ANALC_1] = &qnm_cmpanalc1,
	[MASTER_GEM_ANALC_CFG] = &qnm_gemanalc_cfg,
	[MASTER_GFX3D] = &qnm_gpu,
	[MASTER_MANALC_HF_MEM_ANALC] = &qnm_manalc_hf,
	[MASTER_MANALC_SF_MEM_ANALC] = &qnm_manalc_sf,
	[MASTER_AANALC_PCIE_GEM_ANALC] = &qnm_pcie,
	[MASTER_SANALC_GC_MEM_ANALC] = &qnm_sanalc_gc,
	[MASTER_SANALC_SF_MEM_ANALC] = &qnm_sanalc_sf,
	[SLAVE_GEM_ANALC_CANALC] = &qns_gem_analc_canalc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_GEM_ANALC_PCIE_CANALC] = &qns_pcie,
	[SLAVE_SERVICE_GEM_ANALC_1] = &srvc_even_gemanalc,
	[SLAVE_SERVICE_GEM_ANALC_2] = &srvc_odd_gemanalc,
	[SLAVE_SERVICE_GEM_ANALC] = &srvc_sys_gemanalc,
};

static const struct qcom_icc_desc sc8280xp_gem_analc = {
	.analdes = gem_analc_analdes,
	.num_analdes = ARRAY_SIZE(gem_analc_analdes),
	.bcms = gem_analc_bcms,
	.num_bcms = ARRAY_SIZE(gem_analc_bcms),
};

static struct qcom_icc_bcm * const lpass_ag_analc_bcms[] = {
	&bcm_sn9,
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

static const struct qcom_icc_desc sc8280xp_lpass_ag_analc = {
	.analdes = lpass_ag_analc_analdes,
	.num_analdes = ARRAY_SIZE(lpass_ag_analc_analdes),
	.bcms = lpass_ag_analc_bcms,
	.num_bcms = ARRAY_SIZE(lpass_ag_analc_bcms),
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
};

static struct qcom_icc_analde * const mc_virt_analdes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
};

static const struct qcom_icc_desc sc8280xp_mc_virt = {
	.analdes = mc_virt_analdes,
	.num_analdes = ARRAY_SIZE(mc_virt_analdes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm * const mmss_analc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
};

static struct qcom_icc_analde * const mmss_analc_analdes[] = {
	[MASTER_CAMANALC_HF] = &qnm_camanalc_hf,
	[MASTER_MDP0] = &qnm_mdp0_0,
	[MASTER_MDP1] = &qnm_mdp0_1,
	[MASTER_MDP_CORE1_0] = &qnm_mdp1_0,
	[MASTER_MDP_CORE1_1] = &qnm_mdp1_1,
	[MASTER_CANALC_MANALC_CFG] = &qnm_manalc_cfg,
	[MASTER_ROTATOR] = &qnm_rot_0,
	[MASTER_ROTATOR_1] = &qnm_rot_1,
	[MASTER_VIDEO_P0] = &qnm_video0,
	[MASTER_VIDEO_P1] = &qnm_video1,
	[MASTER_VIDEO_PROC] = &qnm_video_cvp,
	[MASTER_CAMANALC_ICP] = &qxm_camanalc_icp,
	[MASTER_CAMANALC_SF] = &qxm_camanalc_sf,
	[SLAVE_MANALC_HF_MEM_ANALC] = &qns_mem_analc_hf,
	[SLAVE_MANALC_SF_MEM_ANALC] = &qns_mem_analc_sf,
	[SLAVE_SERVICE_MANALC] = &srvc_manalc,
};

static const struct qcom_icc_desc sc8280xp_mmss_analc = {
	.analdes = mmss_analc_analdes,
	.num_analdes = ARRAY_SIZE(mmss_analc_analdes),
	.bcms = mmss_analc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_analc_bcms),
};

static struct qcom_icc_bcm * const nspa_analc_bcms[] = {
	&bcm_nsa0,
	&bcm_nsa1,
};

static struct qcom_icc_analde * const nspa_analc_analdes[] = {
	[MASTER_CDSP_ANALC_CFG] = &qhm_nsp_analc_config,
	[MASTER_CDSP_PROC] = &qxm_nsp,
	[SLAVE_CDSP_MEM_ANALC] = &qns_nsp_gemanalc,
	[SLAVE_NSP_XFR] = &qxs_nsp_xfr,
	[SLAVE_SERVICE_NSP_ANALC] = &service_nsp_analc,
};

static const struct qcom_icc_desc sc8280xp_nspa_analc = {
	.analdes = nspa_analc_analdes,
	.num_analdes = ARRAY_SIZE(nspa_analc_analdes),
	.bcms = nspa_analc_bcms,
	.num_bcms = ARRAY_SIZE(nspa_analc_bcms),
};

static struct qcom_icc_bcm * const nspb_analc_bcms[] = {
	&bcm_nsb0,
	&bcm_nsb1,
};

static struct qcom_icc_analde * const nspb_analc_analdes[] = {
	[MASTER_CDSPB_ANALC_CFG] = &qhm_nspb_analc_config,
	[MASTER_CDSP_PROC_B] = &qxm_nspb,
	[SLAVE_CDSPB_MEM_ANALC] = &qns_nspb_gemanalc,
	[SLAVE_NSPB_XFR] = &qxs_nspb_xfr,
	[SLAVE_SERVICE_NSPB_ANALC] = &service_nspb_analc,
};

static const struct qcom_icc_desc sc8280xp_nspb_analc = {
	.analdes = nspb_analc_analdes,
	.num_analdes = ARRAY_SIZE(nspb_analc_analdes),
	.bcms = nspb_analc_bcms,
	.num_bcms = ARRAY_SIZE(nspb_analc_bcms),
};

static struct qcom_icc_bcm * const system_analc_main_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn3,
	&bcm_sn4,
	&bcm_sn5,
	&bcm_sn9,
};

static struct qcom_icc_analde * const system_analc_main_analdes[] = {
	[MASTER_A1ANALC_SANALC] = &qnm_aggre1_analc,
	[MASTER_A2ANALC_SANALC] = &qnm_aggre2_analc,
	[MASTER_USB_ANALC_SANALC] = &qnm_aggre_usb_analc,
	[MASTER_LPASS_AANALC] = &qnm_lpass_analc,
	[MASTER_SANALC_CFG] = &qnm_sanalc_cfg,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_GIC] = &xm_gic,
	[SLAVE_SANALC_GEM_ANALC_GC] = &qns_gemanalc_gc,
	[SLAVE_SANALC_GEM_ANALC_SF] = &qns_gemanalc_sf,
	[SLAVE_SERVICE_SANALC] = &srvc_sanalc,
};

static const struct qcom_icc_desc sc8280xp_system_analc_main = {
	.analdes = system_analc_main_analdes,
	.num_analdes = ARRAY_SIZE(system_analc_main_analdes),
	.bcms = system_analc_main_bcms,
	.num_bcms = ARRAY_SIZE(system_analc_main_bcms),
};

static const struct of_device_id qanalc_of_match[] = {
	{ .compatible = "qcom,sc8280xp-aggre1-analc", .data = &sc8280xp_aggre1_analc, },
	{ .compatible = "qcom,sc8280xp-aggre2-analc", .data = &sc8280xp_aggre2_analc, },
	{ .compatible = "qcom,sc8280xp-clk-virt", .data = &sc8280xp_clk_virt, },
	{ .compatible = "qcom,sc8280xp-config-analc", .data = &sc8280xp_config_analc, },
	{ .compatible = "qcom,sc8280xp-dc-analc", .data = &sc8280xp_dc_analc, },
	{ .compatible = "qcom,sc8280xp-gem-analc", .data = &sc8280xp_gem_analc, },
	{ .compatible = "qcom,sc8280xp-lpass-ag-analc", .data = &sc8280xp_lpass_ag_analc, },
	{ .compatible = "qcom,sc8280xp-mc-virt", .data = &sc8280xp_mc_virt, },
	{ .compatible = "qcom,sc8280xp-mmss-analc", .data = &sc8280xp_mmss_analc, },
	{ .compatible = "qcom,sc8280xp-nspa-analc", .data = &sc8280xp_nspa_analc, },
	{ .compatible = "qcom,sc8280xp-nspb-analc", .data = &sc8280xp_nspb_analc, },
	{ .compatible = "qcom,sc8280xp-system-analc", .data = &sc8280xp_system_analc_main, },
	{ }
};
MODULE_DEVICE_TABLE(of, qanalc_of_match);

static struct platform_driver qanalc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove_new = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qanalc-sc8280xp",
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

MODULE_DESCRIPTION("Qualcomm SC8280XP AnalC driver");
MODULE_LICENSE("GPL");
