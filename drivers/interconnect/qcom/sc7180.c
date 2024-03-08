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
#include <dt-bindings/interconnect/qcom,sc7180.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"
#include "sc7180.h"

static struct qcom_icc_analde qhm_a1analc_cfg = {
	.name = "qhm_a1analc_cfg",
	.id = SC7180_MASTER_A1ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC7180_SLAVE_SERVICE_A1ANALC },
};

static struct qcom_icc_analde qhm_qspi = {
	.name = "qhm_qspi",
	.id = SC7180_MASTER_QSPI,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC7180_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qup_0 = {
	.name = "qhm_qup_0",
	.id = SC7180_MASTER_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC7180_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_sdc2 = {
	.name = "xm_sdc2",
	.id = SC7180_MASTER_SDCC_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC7180_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_emmc = {
	.name = "xm_emmc",
	.id = SC7180_MASTER_EMMC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC7180_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.id = SC7180_MASTER_UFS_MEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC7180_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qhm_a2analc_cfg = {
	.name = "qhm_a2analc_cfg",
	.id = SC7180_MASTER_A2ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC7180_SLAVE_SERVICE_A2ANALC },
};

static struct qcom_icc_analde qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = SC7180_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC7180_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qup_1 = {
	.name = "qhm_qup_1",
	.id = SC7180_MASTER_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC7180_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qxm_crypto = {
	.name = "qxm_crypto",
	.id = SC7180_MASTER_CRYPTO,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC7180_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qxm_ipa = {
	.name = "qxm_ipa",
	.id = SC7180_MASTER_IPA,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC7180_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde xm_qdss_etr = {
	.name = "xm_qdss_etr",
	.id = SC7180_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC7180_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qhm_usb3 = {
	.name = "qhm_usb3",
	.id = SC7180_MASTER_USB3,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC7180_SLAVE_A2ANALC_SANALC },
};

static struct qcom_icc_analde qxm_camanalc_hf0_uncomp = {
	.name = "qxm_camanalc_hf0_uncomp",
	.id = SC7180_MASTER_CAMANALC_HF0_UNCOMP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC7180_SLAVE_CAMANALC_UNCOMP },
};

static struct qcom_icc_analde qxm_camanalc_hf1_uncomp = {
	.name = "qxm_camanalc_hf1_uncomp",
	.id = SC7180_MASTER_CAMANALC_HF1_UNCOMP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC7180_SLAVE_CAMANALC_UNCOMP },
};

static struct qcom_icc_analde qxm_camanalc_sf_uncomp = {
	.name = "qxm_camanalc_sf_uncomp",
	.id = SC7180_MASTER_CAMANALC_SF_UNCOMP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC7180_SLAVE_CAMANALC_UNCOMP },
};

static struct qcom_icc_analde qnm_npu = {
	.name = "qnm_npu",
	.id = SC7180_MASTER_NPU,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC7180_SLAVE_CDSP_GEM_ANALC },
};

static struct qcom_icc_analde qxm_npu_dsp = {
	.name = "qxm_npu_dsp",
	.id = SC7180_MASTER_NPU_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC7180_SLAVE_CDSP_GEM_ANALC },
};

static struct qcom_icc_analde qnm_sanalc = {
	.name = "qnm_sanalc",
	.id = SC7180_MASTER_SANALC_CANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 51,
	.links = { SC7180_SLAVE_A1ANALC_CFG,
		   SC7180_SLAVE_A2ANALC_CFG,
		   SC7180_SLAVE_AHB2PHY_SOUTH,
		   SC7180_SLAVE_AHB2PHY_CENTER,
		   SC7180_SLAVE_AOP,
		   SC7180_SLAVE_AOSS,
		   SC7180_SLAVE_BOOT_ROM,
		   SC7180_SLAVE_CAMERA_CFG,
		   SC7180_SLAVE_CAMERA_NRT_THROTTLE_CFG,
		   SC7180_SLAVE_CAMERA_RT_THROTTLE_CFG,
		   SC7180_SLAVE_CLK_CTL,
		   SC7180_SLAVE_RBCPR_CX_CFG,
		   SC7180_SLAVE_RBCPR_MX_CFG,
		   SC7180_SLAVE_CRYPTO_0_CFG,
		   SC7180_SLAVE_DCC_CFG,
		   SC7180_SLAVE_CANALC_DDRSS,
		   SC7180_SLAVE_DISPLAY_CFG,
		   SC7180_SLAVE_DISPLAY_RT_THROTTLE_CFG,
		   SC7180_SLAVE_DISPLAY_THROTTLE_CFG,
		   SC7180_SLAVE_EMMC_CFG,
		   SC7180_SLAVE_GLM,
		   SC7180_SLAVE_GFX3D_CFG,
		   SC7180_SLAVE_IMEM_CFG,
		   SC7180_SLAVE_IPA_CFG,
		   SC7180_SLAVE_CANALC_MANALC_CFG,
		   SC7180_SLAVE_CANALC_MSS,
		   SC7180_SLAVE_NPU_CFG,
		   SC7180_SLAVE_NPU_DMA_BWMON_CFG,
		   SC7180_SLAVE_NPU_PROC_BWMON_CFG,
		   SC7180_SLAVE_PDM,
		   SC7180_SLAVE_PIMEM_CFG,
		   SC7180_SLAVE_PRNG,
		   SC7180_SLAVE_QDSS_CFG,
		   SC7180_SLAVE_QM_CFG,
		   SC7180_SLAVE_QM_MPU_CFG,
		   SC7180_SLAVE_QSPI_0,
		   SC7180_SLAVE_QUP_0,
		   SC7180_SLAVE_QUP_1,
		   SC7180_SLAVE_SDCC_2,
		   SC7180_SLAVE_SECURITY,
		   SC7180_SLAVE_SANALC_CFG,
		   SC7180_SLAVE_TCSR,
		   SC7180_SLAVE_TLMM_WEST,
		   SC7180_SLAVE_TLMM_ANALRTH,
		   SC7180_SLAVE_TLMM_SOUTH,
		   SC7180_SLAVE_UFS_MEM_CFG,
		   SC7180_SLAVE_USB3,
		   SC7180_SLAVE_VENUS_CFG,
		   SC7180_SLAVE_VENUS_THROTTLE_CFG,
		   SC7180_SLAVE_VSENSE_CTRL_CFG,
		   SC7180_SLAVE_SERVICE_CANALC
	},
};

static struct qcom_icc_analde xm_qdss_dap = {
	.name = "xm_qdss_dap",
	.id = SC7180_MASTER_QDSS_DAP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 51,
	.links = { SC7180_SLAVE_A1ANALC_CFG,
		   SC7180_SLAVE_A2ANALC_CFG,
		   SC7180_SLAVE_AHB2PHY_SOUTH,
		   SC7180_SLAVE_AHB2PHY_CENTER,
		   SC7180_SLAVE_AOP,
		   SC7180_SLAVE_AOSS,
		   SC7180_SLAVE_BOOT_ROM,
		   SC7180_SLAVE_CAMERA_CFG,
		   SC7180_SLAVE_CAMERA_NRT_THROTTLE_CFG,
		   SC7180_SLAVE_CAMERA_RT_THROTTLE_CFG,
		   SC7180_SLAVE_CLK_CTL,
		   SC7180_SLAVE_RBCPR_CX_CFG,
		   SC7180_SLAVE_RBCPR_MX_CFG,
		   SC7180_SLAVE_CRYPTO_0_CFG,
		   SC7180_SLAVE_DCC_CFG,
		   SC7180_SLAVE_CANALC_DDRSS,
		   SC7180_SLAVE_DISPLAY_CFG,
		   SC7180_SLAVE_DISPLAY_RT_THROTTLE_CFG,
		   SC7180_SLAVE_DISPLAY_THROTTLE_CFG,
		   SC7180_SLAVE_EMMC_CFG,
		   SC7180_SLAVE_GLM,
		   SC7180_SLAVE_GFX3D_CFG,
		   SC7180_SLAVE_IMEM_CFG,
		   SC7180_SLAVE_IPA_CFG,
		   SC7180_SLAVE_CANALC_MANALC_CFG,
		   SC7180_SLAVE_CANALC_MSS,
		   SC7180_SLAVE_NPU_CFG,
		   SC7180_SLAVE_NPU_DMA_BWMON_CFG,
		   SC7180_SLAVE_NPU_PROC_BWMON_CFG,
		   SC7180_SLAVE_PDM,
		   SC7180_SLAVE_PIMEM_CFG,
		   SC7180_SLAVE_PRNG,
		   SC7180_SLAVE_QDSS_CFG,
		   SC7180_SLAVE_QM_CFG,
		   SC7180_SLAVE_QM_MPU_CFG,
		   SC7180_SLAVE_QSPI_0,
		   SC7180_SLAVE_QUP_0,
		   SC7180_SLAVE_QUP_1,
		   SC7180_SLAVE_SDCC_2,
		   SC7180_SLAVE_SECURITY,
		   SC7180_SLAVE_SANALC_CFG,
		   SC7180_SLAVE_TCSR,
		   SC7180_SLAVE_TLMM_WEST,
		   SC7180_SLAVE_TLMM_ANALRTH,
		   SC7180_SLAVE_TLMM_SOUTH,
		   SC7180_SLAVE_UFS_MEM_CFG,
		   SC7180_SLAVE_USB3,
		   SC7180_SLAVE_VENUS_CFG,
		   SC7180_SLAVE_VENUS_THROTTLE_CFG,
		   SC7180_SLAVE_VSENSE_CTRL_CFG,
		   SC7180_SLAVE_SERVICE_CANALC
	},
};

static struct qcom_icc_analde qhm_canalc_dc_analc = {
	.name = "qhm_canalc_dc_analc",
	.id = SC7180_MASTER_CANALC_DC_ANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.links = { SC7180_SLAVE_GEM_ANALC_CFG,
		   SC7180_SLAVE_LLCC_CFG
	},
};

static struct qcom_icc_analde acm_apps0 = {
	.name = "acm_apps0",
	.id = SC7180_MASTER_APPSS_PROC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 2,
	.links = { SC7180_SLAVE_GEM_ANALC_SANALC,
		   SC7180_SLAVE_LLCC
	},
};

static struct qcom_icc_analde acm_sys_tcu = {
	.name = "acm_sys_tcu",
	.id = SC7180_MASTER_SYS_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SC7180_SLAVE_GEM_ANALC_SANALC,
		   SC7180_SLAVE_LLCC
	},
};

static struct qcom_icc_analde qhm_gemanalc_cfg = {
	.name = "qhm_gemanalc_cfg",
	.id = SC7180_MASTER_GEM_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.links = { SC7180_SLAVE_MSS_PROC_MS_MPU_CFG,
		   SC7180_SLAVE_SERVICE_GEM_ANALC
	},
};

static struct qcom_icc_analde qnm_cmpanalc = {
	.name = "qnm_cmpanalc",
	.id = SC7180_MASTER_COMPUTE_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 2,
	.links = { SC7180_SLAVE_GEM_ANALC_SANALC,
		   SC7180_SLAVE_LLCC
	},
};

static struct qcom_icc_analde qnm_manalc_hf = {
	.name = "qnm_manalc_hf",
	.id = SC7180_MASTER_MANALC_HF_MEM_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC7180_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_manalc_sf = {
	.name = "qnm_manalc_sf",
	.id = SC7180_MASTER_MANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 2,
	.links = { SC7180_SLAVE_GEM_ANALC_SANALC,
		   SC7180_SLAVE_LLCC
	},
};

static struct qcom_icc_analde qnm_sanalc_gc = {
	.name = "qnm_sanalc_gc",
	.id = SC7180_MASTER_SANALC_GC_MEM_ANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC7180_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_sanalc_sf = {
	.name = "qnm_sanalc_sf",
	.id = SC7180_MASTER_SANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC7180_SLAVE_LLCC },
};

static struct qcom_icc_analde qxm_gpu = {
	.name = "qxm_gpu",
	.id = SC7180_MASTER_GFX3D,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SC7180_SLAVE_GEM_ANALC_SANALC,
		   SC7180_SLAVE_LLCC
	},
};

static struct qcom_icc_analde llcc_mc = {
	.name = "llcc_mc",
	.id = SC7180_MASTER_LLCC,
	.channels = 2,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC7180_SLAVE_EBI1 },
};

static struct qcom_icc_analde qhm_manalc_cfg = {
	.name = "qhm_manalc_cfg",
	.id = SC7180_MASTER_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC7180_SLAVE_SERVICE_MANALC },
};

static struct qcom_icc_analde qxm_camanalc_hf0 = {
	.name = "qxm_camanalc_hf0",
	.id = SC7180_MASTER_CAMANALC_HF0,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC7180_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_camanalc_hf1 = {
	.name = "qxm_camanalc_hf1",
	.id = SC7180_MASTER_CAMANALC_HF1,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC7180_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_camanalc_sf = {
	.name = "qxm_camanalc_sf",
	.id = SC7180_MASTER_CAMANALC_SF,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC7180_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_mdp0 = {
	.name = "qxm_mdp0",
	.id = SC7180_MASTER_MDP0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC7180_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_rot = {
	.name = "qxm_rot",
	.id = SC7180_MASTER_ROTATOR,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC7180_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_venus0 = {
	.name = "qxm_venus0",
	.id = SC7180_MASTER_VIDEO_P0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC7180_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_venus_arm9 = {
	.name = "qxm_venus_arm9",
	.id = SC7180_MASTER_VIDEO_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC7180_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde amm_npu_sys = {
	.name = "amm_npu_sys",
	.id = SC7180_MASTER_NPU_SYS,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC7180_SLAVE_NPU_COMPUTE_ANALC },
};

static struct qcom_icc_analde qhm_npu_cfg = {
	.name = "qhm_npu_cfg",
	.id = SC7180_MASTER_NPU_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 8,
	.links = { SC7180_SLAVE_NPU_CAL_DP0,
		   SC7180_SLAVE_NPU_CP,
		   SC7180_SLAVE_NPU_INT_DMA_BWMON_CFG,
		   SC7180_SLAVE_NPU_DPM,
		   SC7180_SLAVE_ISENSE_CFG,
		   SC7180_SLAVE_NPU_LLM_CFG,
		   SC7180_SLAVE_NPU_TCM,
		   SC7180_SLAVE_SERVICE_NPU_ANALC
	},
};

static struct qcom_icc_analde qup_core_master_1 = {
	.name = "qup_core_master_1",
	.id = SC7180_MASTER_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC7180_SLAVE_QUP_CORE_0 },
};

static struct qcom_icc_analde qup_core_master_2 = {
	.name = "qup_core_master_2",
	.id = SC7180_MASTER_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC7180_SLAVE_QUP_CORE_1 },
};

static struct qcom_icc_analde qhm_sanalc_cfg = {
	.name = "qhm_sanalc_cfg",
	.id = SC7180_MASTER_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC7180_SLAVE_SERVICE_SANALC },
};

static struct qcom_icc_analde qnm_aggre1_analc = {
	.name = "qnm_aggre1_analc",
	.id = SC7180_MASTER_A1ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 6,
	.links = { SC7180_SLAVE_APPSS,
		   SC7180_SLAVE_SANALC_CANALC,
		   SC7180_SLAVE_SANALC_GEM_ANALC_SF,
		   SC7180_SLAVE_IMEM,
		   SC7180_SLAVE_PIMEM,
		   SC7180_SLAVE_QDSS_STM
	},
};

static struct qcom_icc_analde qnm_aggre2_analc = {
	.name = "qnm_aggre2_analc",
	.id = SC7180_MASTER_A2ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 7,
	.links = { SC7180_SLAVE_APPSS,
		   SC7180_SLAVE_SANALC_CANALC,
		   SC7180_SLAVE_SANALC_GEM_ANALC_SF,
		   SC7180_SLAVE_IMEM,
		   SC7180_SLAVE_PIMEM,
		   SC7180_SLAVE_QDSS_STM,
		   SC7180_SLAVE_TCU
	},
};

static struct qcom_icc_analde qnm_gemanalc = {
	.name = "qnm_gemanalc",
	.id = SC7180_MASTER_GEM_ANALC_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 6,
	.links = { SC7180_SLAVE_APPSS,
		   SC7180_SLAVE_SANALC_CANALC,
		   SC7180_SLAVE_IMEM,
		   SC7180_SLAVE_PIMEM,
		   SC7180_SLAVE_QDSS_STM,
		   SC7180_SLAVE_TCU
	},
};

static struct qcom_icc_analde qxm_pimem = {
	.name = "qxm_pimem",
	.id = SC7180_MASTER_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SC7180_SLAVE_SANALC_GEM_ANALC_GC,
		   SC7180_SLAVE_IMEM
	},
};

static struct qcom_icc_analde qns_a1analc_sanalc = {
	.name = "qns_a1analc_sanalc",
	.id = SC7180_SLAVE_A1ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC7180_MASTER_A1ANALC_SANALC },
};

static struct qcom_icc_analde srvc_aggre1_analc = {
	.name = "srvc_aggre1_analc",
	.id = SC7180_SLAVE_SERVICE_A1ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_a2analc_sanalc = {
	.name = "qns_a2analc_sanalc",
	.id = SC7180_SLAVE_A2ANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC7180_MASTER_A2ANALC_SANALC },
};

static struct qcom_icc_analde srvc_aggre2_analc = {
	.name = "srvc_aggre2_analc",
	.id = SC7180_SLAVE_SERVICE_A2ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_camanalc_uncomp = {
	.name = "qns_camanalc_uncomp",
	.id = SC7180_SLAVE_CAMANALC_UNCOMP,
	.channels = 1,
	.buswidth = 32,
};

static struct qcom_icc_analde qns_cdsp_gemanalc = {
	.name = "qns_cdsp_gemanalc",
	.id = SC7180_SLAVE_CDSP_GEM_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC7180_MASTER_COMPUTE_ANALC },
};

static struct qcom_icc_analde qhs_a1_analc_cfg = {
	.name = "qhs_a1_analc_cfg",
	.id = SC7180_SLAVE_A1ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC7180_MASTER_A1ANALC_CFG },
};

static struct qcom_icc_analde qhs_a2_analc_cfg = {
	.name = "qhs_a2_analc_cfg",
	.id = SC7180_SLAVE_A2ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC7180_MASTER_A2ANALC_CFG },
};

static struct qcom_icc_analde qhs_ahb2phy0 = {
	.name = "qhs_ahb2phy0",
	.id = SC7180_SLAVE_AHB2PHY_SOUTH,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ahb2phy2 = {
	.name = "qhs_ahb2phy2",
	.id = SC7180_SLAVE_AHB2PHY_CENTER,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_aop = {
	.name = "qhs_aop",
	.id = SC7180_SLAVE_AOP,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_aoss = {
	.name = "qhs_aoss",
	.id = SC7180_SLAVE_AOSS,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_boot_rom = {
	.name = "qhs_boot_rom",
	.id = SC7180_SLAVE_BOOT_ROM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_camera_cfg = {
	.name = "qhs_camera_cfg",
	.id = SC7180_SLAVE_CAMERA_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_camera_nrt_throttle_cfg = {
	.name = "qhs_camera_nrt_throttle_cfg",
	.id = SC7180_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_camera_rt_throttle_cfg = {
	.name = "qhs_camera_rt_throttle_cfg",
	.id = SC7180_SLAVE_CAMERA_RT_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SC7180_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = SC7180_SLAVE_RBCPR_CX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cpr_mx = {
	.name = "qhs_cpr_mx",
	.id = SC7180_SLAVE_RBCPR_MX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = SC7180_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_dcc_cfg = {
	.name = "qhs_dcc_cfg",
	.id = SC7180_SLAVE_DCC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ddrss_cfg = {
	.name = "qhs_ddrss_cfg",
	.id = SC7180_SLAVE_CANALC_DDRSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC7180_MASTER_CANALC_DC_ANALC },
};

static struct qcom_icc_analde qhs_display_cfg = {
	.name = "qhs_display_cfg",
	.id = SC7180_SLAVE_DISPLAY_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_display_rt_throttle_cfg = {
	.name = "qhs_display_rt_throttle_cfg",
	.id = SC7180_SLAVE_DISPLAY_RT_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_display_throttle_cfg = {
	.name = "qhs_display_throttle_cfg",
	.id = SC7180_SLAVE_DISPLAY_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_emmc_cfg = {
	.name = "qhs_emmc_cfg",
	.id = SC7180_SLAVE_EMMC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_glm = {
	.name = "qhs_glm",
	.id = SC7180_SLAVE_GLM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.id = SC7180_SLAVE_GFX3D_CFG,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SC7180_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ipa = {
	.name = "qhs_ipa",
	.id = SC7180_SLAVE_IPA_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_manalc_cfg = {
	.name = "qhs_manalc_cfg",
	.id = SC7180_SLAVE_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC7180_MASTER_CANALC_MANALC_CFG },
};

static struct qcom_icc_analde qhs_mss_cfg = {
	.name = "qhs_mss_cfg",
	.id = SC7180_SLAVE_CANALC_MSS,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_npu_cfg = {
	.name = "qhs_npu_cfg",
	.id = SC7180_SLAVE_NPU_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC7180_MASTER_NPU_ANALC_CFG },
};

static struct qcom_icc_analde qhs_npu_dma_throttle_cfg = {
	.name = "qhs_npu_dma_throttle_cfg",
	.id = SC7180_SLAVE_NPU_DMA_BWMON_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_npu_dsp_throttle_cfg = {
	.name = "qhs_npu_dsp_throttle_cfg",
	.id = SC7180_SLAVE_NPU_PROC_BWMON_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pdm = {
	.name = "qhs_pdm",
	.id = SC7180_SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = SC7180_SLAVE_PIMEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_prng = {
	.name = "qhs_prng",
	.id = SC7180_SLAVE_PRNG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SC7180_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qm_cfg = {
	.name = "qhs_qm_cfg",
	.id = SC7180_SLAVE_QM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qm_mpu_cfg = {
	.name = "qhs_qm_mpu_cfg",
	.id = SC7180_SLAVE_QM_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qspi = {
	.name = "qhs_qspi",
	.id = SC7180_SLAVE_QSPI_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qup0 = {
	.name = "qhs_qup0",
	.id = SC7180_SLAVE_QUP_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qup1 = {
	.name = "qhs_qup1",
	.id = SC7180_SLAVE_QUP_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = SC7180_SLAVE_SDCC_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_security = {
	.name = "qhs_security",
	.id = SC7180_SLAVE_SECURITY,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_sanalc_cfg = {
	.name = "qhs_sanalc_cfg",
	.id = SC7180_SLAVE_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC7180_MASTER_SANALC_CFG },
};

static struct qcom_icc_analde qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SC7180_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tlmm_1 = {
	.name = "qhs_tlmm_1",
	.id = SC7180_SLAVE_TLMM_WEST,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tlmm_2 = {
	.name = "qhs_tlmm_2",
	.id = SC7180_SLAVE_TLMM_ANALRTH,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tlmm_3 = {
	.name = "qhs_tlmm_3",
	.id = SC7180_SLAVE_TLMM_SOUTH,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.id = SC7180_SLAVE_UFS_MEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_usb3 = {
	.name = "qhs_usb3",
	.id = SC7180_SLAVE_USB3,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = SC7180_SLAVE_VENUS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_venus_throttle_cfg = {
	.name = "qhs_venus_throttle_cfg",
	.id = SC7180_SLAVE_VENUS_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.id = SC7180_SLAVE_VSENSE_CTRL_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde srvc_canalc = {
	.name = "srvc_canalc",
	.id = SC7180_SLAVE_SERVICE_CANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_gemanalc = {
	.name = "qhs_gemanalc",
	.id = SC7180_SLAVE_GEM_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SC7180_MASTER_GEM_ANALC_CFG },
};

static struct qcom_icc_analde qhs_llcc = {
	.name = "qhs_llcc",
	.id = SC7180_SLAVE_LLCC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_mdsp_ms_mpu_cfg = {
	.name = "qhs_mdsp_ms_mpu_cfg",
	.id = SC7180_SLAVE_MSS_PROC_MS_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_gem_analc_sanalc = {
	.name = "qns_gem_analc_sanalc",
	.id = SC7180_SLAVE_GEM_ANALC_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC7180_MASTER_GEM_ANALC_SANALC },
};

static struct qcom_icc_analde qns_llcc = {
	.name = "qns_llcc",
	.id = SC7180_SLAVE_LLCC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC7180_MASTER_LLCC },
};

static struct qcom_icc_analde srvc_gemanalc = {
	.name = "srvc_gemanalc",
	.id = SC7180_SLAVE_SERVICE_GEM_ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde ebi = {
	.name = "ebi",
	.id = SC7180_SLAVE_EBI1,
	.channels = 2,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_mem_analc_hf = {
	.name = "qns_mem_analc_hf",
	.id = SC7180_SLAVE_MANALC_HF_MEM_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC7180_MASTER_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qns_mem_analc_sf = {
	.name = "qns_mem_analc_sf",
	.id = SC7180_SLAVE_MANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SC7180_MASTER_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde srvc_manalc = {
	.name = "srvc_manalc",
	.id = SC7180_SLAVE_SERVICE_MANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cal_dp0 = {
	.name = "qhs_cal_dp0",
	.id = SC7180_SLAVE_NPU_CAL_DP0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cp = {
	.name = "qhs_cp",
	.id = SC7180_SLAVE_NPU_CP,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_dma_bwmon = {
	.name = "qhs_dma_bwmon",
	.id = SC7180_SLAVE_NPU_INT_DMA_BWMON_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_dpm = {
	.name = "qhs_dpm",
	.id = SC7180_SLAVE_NPU_DPM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_isense = {
	.name = "qhs_isense",
	.id = SC7180_SLAVE_ISENSE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_llm = {
	.name = "qhs_llm",
	.id = SC7180_SLAVE_NPU_LLM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tcm = {
	.name = "qhs_tcm",
	.id = SC7180_SLAVE_NPU_TCM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_npu_sys = {
	.name = "qns_npu_sys",
	.id = SC7180_SLAVE_NPU_COMPUTE_ANALC,
	.channels = 2,
	.buswidth = 32,
};

static struct qcom_icc_analde srvc_analc = {
	.name = "srvc_analc",
	.id = SC7180_SLAVE_SERVICE_NPU_ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qup_core_slave_1 = {
	.name = "qup_core_slave_1",
	.id = SC7180_SLAVE_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qup_core_slave_2 = {
	.name = "qup_core_slave_2",
	.id = SC7180_SLAVE_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_apss = {
	.name = "qhs_apss",
	.id = SC7180_SLAVE_APPSS,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qns_canalc = {
	.name = "qns_canalc",
	.id = SC7180_SLAVE_SANALC_CANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC7180_MASTER_SANALC_CANALC },
};

static struct qcom_icc_analde qns_gemanalc_gc = {
	.name = "qns_gemanalc_gc",
	.id = SC7180_SLAVE_SANALC_GEM_ANALC_GC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SC7180_MASTER_SANALC_GC_MEM_ANALC },
};

static struct qcom_icc_analde qns_gemanalc_sf = {
	.name = "qns_gemanalc_sf",
	.id = SC7180_SLAVE_SANALC_GEM_ANALC_SF,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SC7180_MASTER_SANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxs_imem = {
	.name = "qxs_imem",
	.id = SC7180_SLAVE_IMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qxs_pimem = {
	.name = "qxs_pimem",
	.id = SC7180_SLAVE_PIMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde srvc_sanalc = {
	.name = "srvc_sanalc",
	.id = SC7180_SLAVE_SERVICE_SANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SC7180_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SC7180_SLAVE_TCU,
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

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.keepalive = true,
	.num_analdes = 48,
	.analdes = { &qnm_sanalc,
		   &xm_qdss_dap,
		   &qhs_a1_analc_cfg,
		   &qhs_a2_analc_cfg,
		   &qhs_ahb2phy0,
		   &qhs_aop,
		   &qhs_aoss,
		   &qhs_boot_rom,
		   &qhs_camera_cfg,
		   &qhs_camera_nrt_throttle_cfg,
		   &qhs_camera_rt_throttle_cfg,
		   &qhs_clk_ctl,
		   &qhs_cpr_cx,
		   &qhs_cpr_mx,
		   &qhs_crypto0_cfg,
		   &qhs_dcc_cfg,
		   &qhs_ddrss_cfg,
		   &qhs_display_cfg,
		   &qhs_display_rt_throttle_cfg,
		   &qhs_display_throttle_cfg,
		   &qhs_glm,
		   &qhs_gpuss_cfg,
		   &qhs_imem_cfg,
		   &qhs_ipa,
		   &qhs_manalc_cfg,
		   &qhs_mss_cfg,
		   &qhs_npu_cfg,
		   &qhs_npu_dma_throttle_cfg,
		   &qhs_npu_dsp_throttle_cfg,
		   &qhs_pimem_cfg,
		   &qhs_prng,
		   &qhs_qdss_cfg,
		   &qhs_qm_cfg,
		   &qhs_qm_mpu_cfg,
		   &qhs_qup0,
		   &qhs_qup1,
		   &qhs_security,
		   &qhs_sanalc_cfg,
		   &qhs_tcsr,
		   &qhs_tlmm_1,
		   &qhs_tlmm_2,
		   &qhs_tlmm_3,
		   &qhs_ufs_mem_cfg,
		   &qhs_usb3,
		   &qhs_venus_cfg,
		   &qhs_venus_throttle_cfg,
		   &qhs_vsense_ctrl_cfg,
		   &srvc_canalc
	},
};

static struct qcom_icc_bcm bcm_mm1 = {
	.name = "MM1",
	.keepalive = false,
	.num_analdes = 8,
	.analdes = { &qxm_camanalc_hf0_uncomp,
		   &qxm_camanalc_hf1_uncomp,
		   &qxm_camanalc_sf_uncomp,
		   &qhm_manalc_cfg,
		   &qxm_mdp0,
		   &qxm_rot,
		   &qxm_venus0,
		   &qxm_venus_arm9
	},
};

static struct qcom_icc_bcm bcm_sh2 = {
	.name = "SH2",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &acm_sys_tcu },
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
	.num_analdes = 2,
	.analdes = { &qup_core_master_1, &qup_core_master_2 },
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
	.analdes = { &acm_apps0 },
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
	.analdes = { &qns_cdsp_gemanalc },
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qxs_imem },
};

static struct qcom_icc_bcm bcm_cn1 = {
	.name = "CN1",
	.keepalive = false,
	.num_analdes = 8,
	.analdes = { &qhm_qspi,
		   &xm_sdc2,
		   &xm_emmc,
		   &qhs_ahb2phy2,
		   &qhs_emmc_cfg,
		   &qhs_pdm,
		   &qhs_qspi,
		   &qhs_sdc2
	},
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.keepalive = false,
	.num_analdes = 2,
	.analdes = { &qxm_pimem, &qns_gemanalc_gc },
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

static struct qcom_icc_bcm bcm_co3 = {
	.name = "CO3",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qxm_npu_dsp },
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &xs_qdss_stm },
};

static struct qcom_icc_bcm bcm_sn7 = {
	.name = "SN7",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qnm_aggre1_analc },
};

static struct qcom_icc_bcm bcm_sn9 = {
	.name = "SN9",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qnm_aggre2_analc },
};

static struct qcom_icc_bcm bcm_sn12 = {
	.name = "SN12",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qnm_gemanalc },
};

static struct qcom_icc_bcm * const aggre1_analc_bcms[] = {
	&bcm_cn1,
};

static struct qcom_icc_analde * const aggre1_analc_analdes[] = {
	[MASTER_A1ANALC_CFG] = &qhm_a1analc_cfg,
	[MASTER_QSPI] = &qhm_qspi,
	[MASTER_QUP_0] = &qhm_qup_0,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_EMMC] = &xm_emmc,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[SLAVE_A1ANALC_SANALC] = &qns_a1analc_sanalc,
	[SLAVE_SERVICE_A1ANALC] = &srvc_aggre1_analc,
};

static const struct qcom_icc_desc sc7180_aggre1_analc = {
	.analdes = aggre1_analc_analdes,
	.num_analdes = ARRAY_SIZE(aggre1_analc_analdes),
	.bcms = aggre1_analc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_analc_bcms),
};

static struct qcom_icc_bcm * const aggre2_analc_bcms[] = {
	&bcm_ce0,
};

static struct qcom_icc_analde * const aggre2_analc_analdes[] = {
	[MASTER_A2ANALC_CFG] = &qhm_a2analc_cfg,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QUP_1] = &qhm_qup_1,
	[MASTER_USB3] = &qhm_usb3,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[SLAVE_A2ANALC_SANALC] = &qns_a2analc_sanalc,
	[SLAVE_SERVICE_A2ANALC] = &srvc_aggre2_analc,
};

static const struct qcom_icc_desc sc7180_aggre2_analc = {
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

static const struct qcom_icc_desc sc7180_camanalc_virt = {
	.analdes = camanalc_virt_analdes,
	.num_analdes = ARRAY_SIZE(camanalc_virt_analdes),
	.bcms = camanalc_virt_bcms,
	.num_bcms = ARRAY_SIZE(camanalc_virt_bcms),
};

static struct qcom_icc_bcm * const compute_analc_bcms[] = {
	&bcm_co0,
	&bcm_co2,
	&bcm_co3,
};

static struct qcom_icc_analde * const compute_analc_analdes[] = {
	[MASTER_NPU] = &qnm_npu,
	[MASTER_NPU_PROC] = &qxm_npu_dsp,
	[SLAVE_CDSP_GEM_ANALC] = &qns_cdsp_gemanalc,
};

static const struct qcom_icc_desc sc7180_compute_analc = {
	.analdes = compute_analc_analdes,
	.num_analdes = ARRAY_SIZE(compute_analc_analdes),
	.bcms = compute_analc_bcms,
	.num_bcms = ARRAY_SIZE(compute_analc_bcms),
};

static struct qcom_icc_bcm * const config_analc_bcms[] = {
	&bcm_cn0,
	&bcm_cn1,
};

static struct qcom_icc_analde * const config_analc_analdes[] = {
	[MASTER_SANALC_CANALC] = &qnm_sanalc,
	[MASTER_QDSS_DAP] = &xm_qdss_dap,
	[SLAVE_A1ANALC_CFG] = &qhs_a1_analc_cfg,
	[SLAVE_A2ANALC_CFG] = &qhs_a2_analc_cfg,
	[SLAVE_AHB2PHY_SOUTH] = &qhs_ahb2phy0,
	[SLAVE_AHB2PHY_CENTER] = &qhs_ahb2phy2,
	[SLAVE_AOP] = &qhs_aop,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_BOOT_ROM] = &qhs_boot_rom,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CAMERA_NRT_THROTTLE_CFG] = &qhs_camera_nrt_throttle_cfg,
	[SLAVE_CAMERA_RT_THROTTLE_CFG] = &qhs_camera_rt_throttle_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MX_CFG] = &qhs_cpr_mx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_DCC_CFG] = &qhs_dcc_cfg,
	[SLAVE_CANALC_DDRSS] = &qhs_ddrss_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_DISPLAY_RT_THROTTLE_CFG] = &qhs_display_rt_throttle_cfg,
	[SLAVE_DISPLAY_THROTTLE_CFG] = &qhs_display_throttle_cfg,
	[SLAVE_EMMC_CFG] = &qhs_emmc_cfg,
	[SLAVE_GLM] = &qhs_glm,
	[SLAVE_GFX3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_CANALC_MANALC_CFG] = &qhs_manalc_cfg,
	[SLAVE_CANALC_MSS] = &qhs_mss_cfg,
	[SLAVE_NPU_CFG] = &qhs_npu_cfg,
	[SLAVE_NPU_DMA_BWMON_CFG] = &qhs_npu_dma_throttle_cfg,
	[SLAVE_NPU_PROC_BWMON_CFG] = &qhs_npu_dsp_throttle_cfg,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QM_CFG] = &qhs_qm_cfg,
	[SLAVE_QM_MPU_CFG] = &qhs_qm_mpu_cfg,
	[SLAVE_QSPI_0] = &qhs_qspi,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SECURITY] = &qhs_security,
	[SLAVE_SANALC_CFG] = &qhs_sanalc_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM_WEST] = &qhs_tlmm_1,
	[SLAVE_TLMM_ANALRTH] = &qhs_tlmm_2,
	[SLAVE_TLMM_SOUTH] = &qhs_tlmm_3,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB3] = &qhs_usb3,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VENUS_THROTTLE_CFG] = &qhs_venus_throttle_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_SERVICE_CANALC] = &srvc_canalc,
};

static const struct qcom_icc_desc sc7180_config_analc = {
	.analdes = config_analc_analdes,
	.num_analdes = ARRAY_SIZE(config_analc_analdes),
	.bcms = config_analc_bcms,
	.num_bcms = ARRAY_SIZE(config_analc_bcms),
};

static struct qcom_icc_analde * const dc_analc_analdes[] = {
	[MASTER_CANALC_DC_ANALC] = &qhm_canalc_dc_analc,
	[SLAVE_GEM_ANALC_CFG] = &qhs_gemanalc,
	[SLAVE_LLCC_CFG] = &qhs_llcc,
};

static const struct qcom_icc_desc sc7180_dc_analc = {
	.analdes = dc_analc_analdes,
	.num_analdes = ARRAY_SIZE(dc_analc_analdes),
};

static struct qcom_icc_bcm * const gem_analc_bcms[] = {
	&bcm_sh0,
	&bcm_sh2,
	&bcm_sh3,
	&bcm_sh4,
};

static struct qcom_icc_analde * const gem_analc_analdes[] = {
	[MASTER_APPSS_PROC] = &acm_apps0,
	[MASTER_SYS_TCU] = &acm_sys_tcu,
	[MASTER_GEM_ANALC_CFG] = &qhm_gemanalc_cfg,
	[MASTER_COMPUTE_ANALC] = &qnm_cmpanalc,
	[MASTER_MANALC_HF_MEM_ANALC] = &qnm_manalc_hf,
	[MASTER_MANALC_SF_MEM_ANALC] = &qnm_manalc_sf,
	[MASTER_SANALC_GC_MEM_ANALC] = &qnm_sanalc_gc,
	[MASTER_SANALC_SF_MEM_ANALC] = &qnm_sanalc_sf,
	[MASTER_GFX3D] = &qxm_gpu,
	[SLAVE_MSS_PROC_MS_MPU_CFG] = &qhs_mdsp_ms_mpu_cfg,
	[SLAVE_GEM_ANALC_SANALC] = &qns_gem_analc_sanalc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_SERVICE_GEM_ANALC] = &srvc_gemanalc,
};

static const struct qcom_icc_desc sc7180_gem_analc = {
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
	[SLAVE_EBI1] = &ebi,
};

static const struct qcom_icc_desc sc7180_mc_virt = {
	.analdes = mc_virt_analdes,
	.num_analdes = ARRAY_SIZE(mc_virt_analdes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm * const mmss_analc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
	&bcm_mm2,
};

static struct qcom_icc_analde * const mmss_analc_analdes[] = {
	[MASTER_CANALC_MANALC_CFG] = &qhm_manalc_cfg,
	[MASTER_CAMANALC_HF0] = &qxm_camanalc_hf0,
	[MASTER_CAMANALC_HF1] = &qxm_camanalc_hf1,
	[MASTER_CAMANALC_SF] = &qxm_camanalc_sf,
	[MASTER_MDP0] = &qxm_mdp0,
	[MASTER_ROTATOR] = &qxm_rot,
	[MASTER_VIDEO_P0] = &qxm_venus0,
	[MASTER_VIDEO_PROC] = &qxm_venus_arm9,
	[SLAVE_MANALC_HF_MEM_ANALC] = &qns_mem_analc_hf,
	[SLAVE_MANALC_SF_MEM_ANALC] = &qns_mem_analc_sf,
	[SLAVE_SERVICE_MANALC] = &srvc_manalc,
};

static const struct qcom_icc_desc sc7180_mmss_analc = {
	.analdes = mmss_analc_analdes,
	.num_analdes = ARRAY_SIZE(mmss_analc_analdes),
	.bcms = mmss_analc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_analc_bcms),
};

static struct qcom_icc_analde * const npu_analc_analdes[] = {
	[MASTER_NPU_SYS] = &amm_npu_sys,
	[MASTER_NPU_ANALC_CFG] = &qhm_npu_cfg,
	[SLAVE_NPU_CAL_DP0] = &qhs_cal_dp0,
	[SLAVE_NPU_CP] = &qhs_cp,
	[SLAVE_NPU_INT_DMA_BWMON_CFG] = &qhs_dma_bwmon,
	[SLAVE_NPU_DPM] = &qhs_dpm,
	[SLAVE_ISENSE_CFG] = &qhs_isense,
	[SLAVE_NPU_LLM_CFG] = &qhs_llm,
	[SLAVE_NPU_TCM] = &qhs_tcm,
	[SLAVE_NPU_COMPUTE_ANALC] = &qns_npu_sys,
	[SLAVE_SERVICE_NPU_ANALC] = &srvc_analc,
};

static const struct qcom_icc_desc sc7180_npu_analc = {
	.analdes = npu_analc_analdes,
	.num_analdes = ARRAY_SIZE(npu_analc_analdes),
};

static struct qcom_icc_bcm * const qup_virt_bcms[] = {
	&bcm_qup0,
};

static struct qcom_icc_analde * const qup_virt_analdes[] = {
	[MASTER_QUP_CORE_0] = &qup_core_master_1,
	[MASTER_QUP_CORE_1] = &qup_core_master_2,
	[SLAVE_QUP_CORE_0] = &qup_core_slave_1,
	[SLAVE_QUP_CORE_1] = &qup_core_slave_2,
};

static const struct qcom_icc_desc sc7180_qup_virt = {
	.analdes = qup_virt_analdes,
	.num_analdes = ARRAY_SIZE(qup_virt_analdes),
	.bcms = qup_virt_bcms,
	.num_bcms = ARRAY_SIZE(qup_virt_bcms),
};

static struct qcom_icc_bcm * const system_analc_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn2,
	&bcm_sn3,
	&bcm_sn4,
	&bcm_sn7,
	&bcm_sn9,
	&bcm_sn12,
};

static struct qcom_icc_analde * const system_analc_analdes[] = {
	[MASTER_SANALC_CFG] = &qhm_sanalc_cfg,
	[MASTER_A1ANALC_SANALC] = &qnm_aggre1_analc,
	[MASTER_A2ANALC_SANALC] = &qnm_aggre2_analc,
	[MASTER_GEM_ANALC_SANALC] = &qnm_gemanalc,
	[MASTER_PIMEM] = &qxm_pimem,
	[SLAVE_APPSS] = &qhs_apss,
	[SLAVE_SANALC_CANALC] = &qns_canalc,
	[SLAVE_SANALC_GEM_ANALC_GC] = &qns_gemanalc_gc,
	[SLAVE_SANALC_GEM_ANALC_SF] = &qns_gemanalc_sf,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SERVICE_SANALC] = &srvc_sanalc,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sc7180_system_analc = {
	.analdes = system_analc_analdes,
	.num_analdes = ARRAY_SIZE(system_analc_analdes),
	.bcms = system_analc_bcms,
	.num_bcms = ARRAY_SIZE(system_analc_bcms),
};

static const struct of_device_id qanalc_of_match[] = {
	{ .compatible = "qcom,sc7180-aggre1-analc",
	  .data = &sc7180_aggre1_analc},
	{ .compatible = "qcom,sc7180-aggre2-analc",
	  .data = &sc7180_aggre2_analc},
	{ .compatible = "qcom,sc7180-camanalc-virt",
	  .data = &sc7180_camanalc_virt},
	{ .compatible = "qcom,sc7180-compute-analc",
	  .data = &sc7180_compute_analc},
	{ .compatible = "qcom,sc7180-config-analc",
	  .data = &sc7180_config_analc},
	{ .compatible = "qcom,sc7180-dc-analc",
	  .data = &sc7180_dc_analc},
	{ .compatible = "qcom,sc7180-gem-analc",
	  .data = &sc7180_gem_analc},
	{ .compatible = "qcom,sc7180-mc-virt",
	  .data = &sc7180_mc_virt},
	{ .compatible = "qcom,sc7180-mmss-analc",
	  .data = &sc7180_mmss_analc},
	{ .compatible = "qcom,sc7180-npu-analc",
	  .data = &sc7180_npu_analc},
	{ .compatible = "qcom,sc7180-qup-virt",
	  .data = &sc7180_qup_virt},
	{ .compatible = "qcom,sc7180-system-analc",
	  .data = &sc7180_system_analc},
	{ }
};
MODULE_DEVICE_TABLE(of, qanalc_of_match);

static struct platform_driver qanalc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove_new = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qanalc-sc7180",
		.of_match_table = qanalc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(qanalc_driver);

MODULE_DESCRIPTION("Qualcomm SC7180 AnalC driver");
MODULE_LICENSE("GPL v2");
