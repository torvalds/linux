// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Luca Weiss <luca.weiss@fairphone.com>
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <dt-bindings/interconnect/qcom,sm6350.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"
#include "sm6350.h"

static struct qcom_icc_analde qhm_a1analc_cfg = {
	.name = "qhm_a1analc_cfg",
	.id = SM6350_MASTER_A1ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_SLAVE_SERVICE_A1ANALC },
};

static struct qcom_icc_analde qhm_qup_0 = {
	.name = "qhm_qup_0",
	.id = SM6350_MASTER_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_A1ANALC_SANALC_SLV },
};

static struct qcom_icc_analde xm_emmc = {
	.name = "xm_emmc",
	.id = SM6350_MASTER_EMMC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_A1ANALC_SANALC_SLV },
};

static struct qcom_icc_analde xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.id = SM6350_MASTER_UFS_MEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_A1ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qhm_a2analc_cfg = {
	.name = "qhm_a2analc_cfg",
	.id = SM6350_MASTER_A2ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_SLAVE_SERVICE_A2ANALC },
};

static struct qcom_icc_analde qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = SM6350_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qhm_qup_1 = {
	.name = "qhm_qup_1",
	.id = SM6350_MASTER_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qxm_crypto = {
	.name = "qxm_crypto",
	.id = SM6350_MASTER_CRYPTO_CORE_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qxm_ipa = {
	.name = "qxm_ipa",
	.id = SM6350_MASTER_IPA,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde xm_qdss_etr = {
	.name = "xm_qdss_etr",
	.id = SM6350_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde xm_sdc2 = {
	.name = "xm_sdc2",
	.id = SM6350_MASTER_SDCC_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde xm_usb3_0 = {
	.name = "xm_usb3_0",
	.id = SM6350_MASTER_USB3,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_A2ANALC_SANALC_SLV },
};

static struct qcom_icc_analde qxm_camanalc_hf0_uncomp = {
	.name = "qxm_camanalc_hf0_uncomp",
	.id = SM6350_MASTER_CAMANALC_HF0_UNCOMP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_SLAVE_CAMANALC_UNCOMP },
};

static struct qcom_icc_analde qxm_camanalc_icp_uncomp = {
	.name = "qxm_camanalc_icp_uncomp",
	.id = SM6350_MASTER_CAMANALC_ICP_UNCOMP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_SLAVE_CAMANALC_UNCOMP },
};

static struct qcom_icc_analde qxm_camanalc_sf_uncomp = {
	.name = "qxm_camanalc_sf_uncomp",
	.id = SM6350_MASTER_CAMANALC_SF_UNCOMP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_SLAVE_CAMANALC_UNCOMP },
};

static struct qcom_icc_analde qup0_core_master = {
	.name = "qup0_core_master",
	.id = SM6350_MASTER_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_SLAVE_QUP_CORE_0 },
};

static struct qcom_icc_analde qup1_core_master = {
	.name = "qup1_core_master",
	.id = SM6350_MASTER_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_SLAVE_QUP_CORE_1 },
};

static struct qcom_icc_analde qnm_npu = {
	.name = "qnm_npu",
	.id = SM6350_MASTER_NPU,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_SLAVE_CDSP_GEM_ANALC },
};

static struct qcom_icc_analde qxm_npu_dsp = {
	.name = "qxm_npu_dsp",
	.id = SM6350_MASTER_NPU_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_SLAVE_CDSP_GEM_ANALC },
};

static struct qcom_icc_analde qnm_sanalc = {
	.name = "qnm_sanalc",
	.id = SM6350_SANALC_CANALC_MAS,
	.channels = 1,
	.buswidth = 8,
	.num_links = 42,
	.links = { SM6350_SLAVE_CAMERA_CFG,
		   SM6350_SLAVE_SDCC_2,
		   SM6350_SLAVE_CANALC_MANALC_CFG,
		   SM6350_SLAVE_UFS_MEM_CFG,
		   SM6350_SLAVE_QM_CFG,
		   SM6350_SLAVE_SANALC_CFG,
		   SM6350_SLAVE_QM_MPU_CFG,
		   SM6350_SLAVE_GLM,
		   SM6350_SLAVE_PDM,
		   SM6350_SLAVE_CAMERA_NRT_THROTTLE_CFG,
		   SM6350_SLAVE_A2ANALC_CFG,
		   SM6350_SLAVE_QDSS_CFG,
		   SM6350_SLAVE_VSENSE_CTRL_CFG,
		   SM6350_SLAVE_CAMERA_RT_THROTTLE_CFG,
		   SM6350_SLAVE_DISPLAY_CFG,
		   SM6350_SLAVE_TCSR,
		   SM6350_SLAVE_DCC_CFG,
		   SM6350_SLAVE_CANALC_DDRSS,
		   SM6350_SLAVE_DISPLAY_THROTTLE_CFG,
		   SM6350_SLAVE_NPU_CFG,
		   SM6350_SLAVE_AHB2PHY,
		   SM6350_SLAVE_GRAPHICS_3D_CFG,
		   SM6350_SLAVE_BOOT_ROM,
		   SM6350_SLAVE_VENUS_CFG,
		   SM6350_SLAVE_IPA_CFG,
		   SM6350_SLAVE_SECURITY,
		   SM6350_SLAVE_IMEM_CFG,
		   SM6350_SLAVE_CANALC_MSS,
		   SM6350_SLAVE_SERVICE_CANALC,
		   SM6350_SLAVE_USB3,
		   SM6350_SLAVE_VENUS_THROTTLE_CFG,
		   SM6350_SLAVE_RBCPR_CX_CFG,
		   SM6350_SLAVE_A1ANALC_CFG,
		   SM6350_SLAVE_AOSS,
		   SM6350_SLAVE_PRNG,
		   SM6350_SLAVE_EMMC_CFG,
		   SM6350_SLAVE_CRYPTO_0_CFG,
		   SM6350_SLAVE_PIMEM_CFG,
		   SM6350_SLAVE_RBCPR_MX_CFG,
		   SM6350_SLAVE_QUP_0,
		   SM6350_SLAVE_QUP_1,
		   SM6350_SLAVE_CLK_CTL
	},
};

static struct qcom_icc_analde xm_qdss_dap = {
	.name = "xm_qdss_dap",
	.id = SM6350_MASTER_QDSS_DAP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 42,
	.links = { SM6350_SLAVE_CAMERA_CFG,
		   SM6350_SLAVE_SDCC_2,
		   SM6350_SLAVE_CANALC_MANALC_CFG,
		   SM6350_SLAVE_UFS_MEM_CFG,
		   SM6350_SLAVE_QM_CFG,
		   SM6350_SLAVE_SANALC_CFG,
		   SM6350_SLAVE_QM_MPU_CFG,
		   SM6350_SLAVE_GLM,
		   SM6350_SLAVE_PDM,
		   SM6350_SLAVE_CAMERA_NRT_THROTTLE_CFG,
		   SM6350_SLAVE_A2ANALC_CFG,
		   SM6350_SLAVE_QDSS_CFG,
		   SM6350_SLAVE_VSENSE_CTRL_CFG,
		   SM6350_SLAVE_CAMERA_RT_THROTTLE_CFG,
		   SM6350_SLAVE_DISPLAY_CFG,
		   SM6350_SLAVE_TCSR,
		   SM6350_SLAVE_DCC_CFG,
		   SM6350_SLAVE_CANALC_DDRSS,
		   SM6350_SLAVE_DISPLAY_THROTTLE_CFG,
		   SM6350_SLAVE_NPU_CFG,
		   SM6350_SLAVE_AHB2PHY,
		   SM6350_SLAVE_GRAPHICS_3D_CFG,
		   SM6350_SLAVE_BOOT_ROM,
		   SM6350_SLAVE_VENUS_CFG,
		   SM6350_SLAVE_IPA_CFG,
		   SM6350_SLAVE_SECURITY,
		   SM6350_SLAVE_IMEM_CFG,
		   SM6350_SLAVE_CANALC_MSS,
		   SM6350_SLAVE_SERVICE_CANALC,
		   SM6350_SLAVE_USB3,
		   SM6350_SLAVE_VENUS_THROTTLE_CFG,
		   SM6350_SLAVE_RBCPR_CX_CFG,
		   SM6350_SLAVE_A1ANALC_CFG,
		   SM6350_SLAVE_AOSS,
		   SM6350_SLAVE_PRNG,
		   SM6350_SLAVE_EMMC_CFG,
		   SM6350_SLAVE_CRYPTO_0_CFG,
		   SM6350_SLAVE_PIMEM_CFG,
		   SM6350_SLAVE_RBCPR_MX_CFG,
		   SM6350_SLAVE_QUP_0,
		   SM6350_SLAVE_QUP_1,
		   SM6350_SLAVE_CLK_CTL
	},
};

static struct qcom_icc_analde qhm_canalc_dc_analc = {
	.name = "qhm_canalc_dc_analc",
	.id = SM6350_MASTER_CANALC_DC_ANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.links = { SM6350_SLAVE_LLCC_CFG,
		   SM6350_SLAVE_GEM_ANALC_CFG
	},
};

static struct qcom_icc_analde acm_apps = {
	.name = "acm_apps",
	.id = SM6350_MASTER_AMPSS_M0,
	.channels = 1,
	.buswidth = 16,
	.num_links = 2,
	.links = { SM6350_SLAVE_LLCC,
		   SM6350_SLAVE_GEM_ANALC_SANALC
	},
};

static struct qcom_icc_analde acm_sys_tcu = {
	.name = "acm_sys_tcu",
	.id = SM6350_MASTER_SYS_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM6350_SLAVE_LLCC,
		   SM6350_SLAVE_GEM_ANALC_SANALC
	},
};

static struct qcom_icc_analde qhm_gemanalc_cfg = {
	.name = "qhm_gemanalc_cfg",
	.id = SM6350_MASTER_GEM_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 3,
	.links = { SM6350_SLAVE_MCDMA_MS_MPU_CFG,
		   SM6350_SLAVE_SERVICE_GEM_ANALC,
		   SM6350_SLAVE_MSS_PROC_MS_MPU_CFG
	},
};

static struct qcom_icc_analde qnm_cmpanalc = {
	.name = "qnm_cmpanalc",
	.id = SM6350_MASTER_COMPUTE_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM6350_SLAVE_LLCC,
		   SM6350_SLAVE_GEM_ANALC_SANALC
	},
};

static struct qcom_icc_analde qnm_manalc_hf = {
	.name = "qnm_manalc_hf",
	.id = SM6350_MASTER_MANALC_HF_MEM_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM6350_SLAVE_LLCC,
		   SM6350_SLAVE_GEM_ANALC_SANALC
	},
};

static struct qcom_icc_analde qnm_manalc_sf = {
	.name = "qnm_manalc_sf",
	.id = SM6350_MASTER_MANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM6350_SLAVE_LLCC,
		   SM6350_SLAVE_GEM_ANALC_SANALC
	},
};

static struct qcom_icc_analde qnm_sanalc_gc = {
	.name = "qnm_sanalc_gc",
	.id = SM6350_MASTER_SANALC_GC_MEM_ANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_sanalc_sf = {
	.name = "qnm_sanalc_sf",
	.id = SM6350_MASTER_SANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM6350_SLAVE_LLCC },
};

static struct qcom_icc_analde qxm_gpu = {
	.name = "qxm_gpu",
	.id = SM6350_MASTER_GRAPHICS_3D,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM6350_SLAVE_LLCC,
		   SM6350_SLAVE_GEM_ANALC_SANALC
	},
};

static struct qcom_icc_analde llcc_mc = {
	.name = "llcc_mc",
	.id = SM6350_MASTER_LLCC,
	.channels = 2,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_SLAVE_EBI_CH0 },
};

static struct qcom_icc_analde qhm_manalc_cfg = {
	.name = "qhm_manalc_cfg",
	.id = SM6350_MASTER_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_SLAVE_SERVICE_MANALC },
};

static struct qcom_icc_analde qnm_video0 = {
	.name = "qnm_video0",
	.id = SM6350_MASTER_VIDEO_P0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qnm_video_cvp = {
	.name = "qnm_video_cvp",
	.id = SM6350_MASTER_VIDEO_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_camanalc_hf = {
	.name = "qxm_camanalc_hf",
	.id = SM6350_MASTER_CAMANALC_HF,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_camanalc_icp = {
	.name = "qxm_camanalc_icp",
	.id = SM6350_MASTER_CAMANALC_ICP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_camanalc_sf = {
	.name = "qxm_camanalc_sf",
	.id = SM6350_MASTER_CAMANALC_SF,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_SLAVE_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxm_mdp0 = {
	.name = "qxm_mdp0",
	.id = SM6350_MASTER_MDP_PORT0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_SLAVE_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde amm_npu_sys = {
	.name = "amm_npu_sys",
	.id = SM6350_MASTER_NPU_SYS,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_SLAVE_NPU_COMPUTE_ANALC },
};

static struct qcom_icc_analde qhm_npu_cfg = {
	.name = "qhm_npu_cfg",
	.id = SM6350_MASTER_NPU_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 8,
	.links = { SM6350_SLAVE_SERVICE_NPU_ANALC,
		   SM6350_SLAVE_ISENSE_CFG,
		   SM6350_SLAVE_NPU_LLM_CFG,
		   SM6350_SLAVE_NPU_INT_DMA_BWMON_CFG,
		   SM6350_SLAVE_NPU_CP,
		   SM6350_SLAVE_NPU_TCM,
		   SM6350_SLAVE_NPU_CAL_DP0,
		   SM6350_SLAVE_NPU_DPM
	},
};

static struct qcom_icc_analde qhm_sanalc_cfg = {
	.name = "qhm_sanalc_cfg",
	.id = SM6350_MASTER_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_SLAVE_SERVICE_SANALC },
};

static struct qcom_icc_analde qnm_aggre1_analc = {
	.name = "qnm_aggre1_analc",
	.id = SM6350_A1ANALC_SANALC_MAS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 6,
	.links = { SM6350_SLAVE_SANALC_GEM_ANALC_SF,
		   SM6350_SLAVE_PIMEM,
		   SM6350_SLAVE_OCIMEM,
		   SM6350_SLAVE_APPSS,
		   SM6350_SANALC_CANALC_SLV,
		   SM6350_SLAVE_QDSS_STM
	},
};

static struct qcom_icc_analde qnm_aggre2_analc = {
	.name = "qnm_aggre2_analc",
	.id = SM6350_A2ANALC_SANALC_MAS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 7,
	.links = { SM6350_SLAVE_SANALC_GEM_ANALC_SF,
		   SM6350_SLAVE_PIMEM,
		   SM6350_SLAVE_OCIMEM,
		   SM6350_SLAVE_APPSS,
		   SM6350_SANALC_CANALC_SLV,
		   SM6350_SLAVE_TCU,
		   SM6350_SLAVE_QDSS_STM
	},
};

static struct qcom_icc_analde qnm_gemanalc = {
	.name = "qnm_gemanalc",
	.id = SM6350_MASTER_GEM_ANALC_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 6,
	.links = { SM6350_SLAVE_PIMEM,
		   SM6350_SLAVE_OCIMEM,
		   SM6350_SLAVE_APPSS,
		   SM6350_SANALC_CANALC_SLV,
		   SM6350_SLAVE_TCU,
		   SM6350_SLAVE_QDSS_STM
	},
};

static struct qcom_icc_analde qxm_pimem = {
	.name = "qxm_pimem",
	.id = SM6350_MASTER_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM6350_SLAVE_SANALC_GEM_ANALC_GC,
		   SM6350_SLAVE_OCIMEM
	},
};

static struct qcom_icc_analde xm_gic = {
	.name = "xm_gic",
	.id = SM6350_MASTER_GIC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_SLAVE_SANALC_GEM_ANALC_GC },
};

static struct qcom_icc_analde qns_a1analc_sanalc = {
	.name = "qns_a1analc_sanalc",
	.id = SM6350_A1ANALC_SANALC_SLV,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM6350_A1ANALC_SANALC_MAS },
};

static struct qcom_icc_analde srvc_aggre1_analc = {
	.name = "srvc_aggre1_analc",
	.id = SM6350_SLAVE_SERVICE_A1ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_a2analc_sanalc = {
	.name = "qns_a2analc_sanalc",
	.id = SM6350_A2ANALC_SANALC_SLV,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM6350_A2ANALC_SANALC_MAS },
};

static struct qcom_icc_analde srvc_aggre2_analc = {
	.name = "srvc_aggre2_analc",
	.id = SM6350_SLAVE_SERVICE_A2ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_camanalc_uncomp = {
	.name = "qns_camanalc_uncomp",
	.id = SM6350_SLAVE_CAMANALC_UNCOMP,
	.channels = 1,
	.buswidth = 32,
};

static struct qcom_icc_analde qup0_core_slave = {
	.name = "qup0_core_slave",
	.id = SM6350_SLAVE_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qup1_core_slave = {
	.name = "qup1_core_slave",
	.id = SM6350_SLAVE_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_cdsp_gemanalc = {
	.name = "qns_cdsp_gemanalc",
	.id = SM6350_SLAVE_CDSP_GEM_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_MASTER_COMPUTE_ANALC },
};

static struct qcom_icc_analde qhs_a1_analc_cfg = {
	.name = "qhs_a1_analc_cfg",
	.id = SM6350_SLAVE_A1ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_MASTER_A1ANALC_CFG },
};

static struct qcom_icc_analde qhs_a2_analc_cfg = {
	.name = "qhs_a2_analc_cfg",
	.id = SM6350_SLAVE_A2ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_MASTER_A2ANALC_CFG },
};

static struct qcom_icc_analde qhs_ahb2phy0 = {
	.name = "qhs_ahb2phy0",
	.id = SM6350_SLAVE_AHB2PHY,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ahb2phy2 = {
	.name = "qhs_ahb2phy2",
	.id = SM6350_SLAVE_AHB2PHY_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_aoss = {
	.name = "qhs_aoss",
	.id = SM6350_SLAVE_AOSS,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_boot_rom = {
	.name = "qhs_boot_rom",
	.id = SM6350_SLAVE_BOOT_ROM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_camera_cfg = {
	.name = "qhs_camera_cfg",
	.id = SM6350_SLAVE_CAMERA_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_camera_nrt_thrott_cfg = {
	.name = "qhs_camera_nrt_thrott_cfg",
	.id = SM6350_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_camera_rt_throttle_cfg = {
	.name = "qhs_camera_rt_throttle_cfg",
	.id = SM6350_SLAVE_CAMERA_RT_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SM6350_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = SM6350_SLAVE_RBCPR_CX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cpr_mx = {
	.name = "qhs_cpr_mx",
	.id = SM6350_SLAVE_RBCPR_MX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = SM6350_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_dcc_cfg = {
	.name = "qhs_dcc_cfg",
	.id = SM6350_SLAVE_DCC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ddrss_cfg = {
	.name = "qhs_ddrss_cfg",
	.id = SM6350_SLAVE_CANALC_DDRSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_MASTER_CANALC_DC_ANALC },
};

static struct qcom_icc_analde qhs_display_cfg = {
	.name = "qhs_display_cfg",
	.id = SM6350_SLAVE_DISPLAY_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_display_throttle_cfg = {
	.name = "qhs_display_throttle_cfg",
	.id = SM6350_SLAVE_DISPLAY_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_emmc_cfg = {
	.name = "qhs_emmc_cfg",
	.id = SM6350_SLAVE_EMMC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_glm = {
	.name = "qhs_glm",
	.id = SM6350_SLAVE_GLM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.id = SM6350_SLAVE_GRAPHICS_3D_CFG,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SM6350_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ipa = {
	.name = "qhs_ipa",
	.id = SM6350_SLAVE_IPA_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_manalc_cfg = {
	.name = "qhs_manalc_cfg",
	.id = SM6350_SLAVE_CANALC_MANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_MASTER_CANALC_MANALC_CFG },
};

static struct qcom_icc_analde qhs_mss_cfg = {
	.name = "qhs_mss_cfg",
	.id = SM6350_SLAVE_CANALC_MSS,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_npu_cfg = {
	.name = "qhs_npu_cfg",
	.id = SM6350_SLAVE_NPU_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_MASTER_NPU_ANALC_CFG },
};

static struct qcom_icc_analde qhs_pdm = {
	.name = "qhs_pdm",
	.id = SM6350_SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = SM6350_SLAVE_PIMEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_prng = {
	.name = "qhs_prng",
	.id = SM6350_SLAVE_PRNG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SM6350_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qm_cfg = {
	.name = "qhs_qm_cfg",
	.id = SM6350_SLAVE_QM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qm_mpu_cfg = {
	.name = "qhs_qm_mpu_cfg",
	.id = SM6350_SLAVE_QM_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qup0 = {
	.name = "qhs_qup0",
	.id = SM6350_SLAVE_QUP_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_qup1 = {
	.name = "qhs_qup1",
	.id = SM6350_SLAVE_QUP_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = SM6350_SLAVE_SDCC_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_security = {
	.name = "qhs_security",
	.id = SM6350_SLAVE_SECURITY,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_sanalc_cfg = {
	.name = "qhs_sanalc_cfg",
	.id = SM6350_SLAVE_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_MASTER_SANALC_CFG },
};

static struct qcom_icc_analde qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SM6350_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.id = SM6350_SLAVE_UFS_MEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_usb3_0 = {
	.name = "qhs_usb3_0",
	.id = SM6350_SLAVE_USB3,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = SM6350_SLAVE_VENUS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_venus_throttle_cfg = {
	.name = "qhs_venus_throttle_cfg",
	.id = SM6350_SLAVE_VENUS_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.id = SM6350_SLAVE_VSENSE_CTRL_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde srvc_canalc = {
	.name = "srvc_canalc",
	.id = SM6350_SLAVE_SERVICE_CANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_gemanalc = {
	.name = "qhs_gemanalc",
	.id = SM6350_SLAVE_GEM_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_MASTER_GEM_ANALC_CFG },
};

static struct qcom_icc_analde qhs_llcc = {
	.name = "qhs_llcc",
	.id = SM6350_SLAVE_LLCC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_mcdma_ms_mpu_cfg = {
	.name = "qhs_mcdma_ms_mpu_cfg",
	.id = SM6350_SLAVE_MCDMA_MS_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_mdsp_ms_mpu_cfg = {
	.name = "qhs_mdsp_ms_mpu_cfg",
	.id = SM6350_SLAVE_MSS_PROC_MS_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_gem_analc_sanalc = {
	.name = "qns_gem_analc_sanalc",
	.id = SM6350_SLAVE_GEM_ANALC_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_MASTER_GEM_ANALC_SANALC },
};

static struct qcom_icc_analde qns_llcc = {
	.name = "qns_llcc",
	.id = SM6350_SLAVE_LLCC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM6350_MASTER_LLCC },
};

static struct qcom_icc_analde srvc_gemanalc = {
	.name = "srvc_gemanalc",
	.id = SM6350_SLAVE_SERVICE_GEM_ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde ebi = {
	.name = "ebi",
	.id = SM6350_SLAVE_EBI_CH0,
	.channels = 2,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_mem_analc_hf = {
	.name = "qns_mem_analc_hf",
	.id = SM6350_SLAVE_MANALC_HF_MEM_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_MASTER_MANALC_HF_MEM_ANALC },
};

static struct qcom_icc_analde qns_mem_analc_sf = {
	.name = "qns_mem_analc_sf",
	.id = SM6350_SLAVE_MANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_MASTER_MANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde srvc_manalc = {
	.name = "srvc_manalc",
	.id = SM6350_SLAVE_SERVICE_MANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cal_dp0 = {
	.name = "qhs_cal_dp0",
	.id = SM6350_SLAVE_NPU_CAL_DP0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_cp = {
	.name = "qhs_cp",
	.id = SM6350_SLAVE_NPU_CP,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_dma_bwmon = {
	.name = "qhs_dma_bwmon",
	.id = SM6350_SLAVE_NPU_INT_DMA_BWMON_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_dpm = {
	.name = "qhs_dpm",
	.id = SM6350_SLAVE_NPU_DPM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_isense = {
	.name = "qhs_isense",
	.id = SM6350_SLAVE_ISENSE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_llm = {
	.name = "qhs_llm",
	.id = SM6350_SLAVE_NPU_LLM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_tcm = {
	.name = "qhs_tcm",
	.id = SM6350_SLAVE_NPU_TCM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qns_npu_sys = {
	.name = "qns_npu_sys",
	.id = SM6350_SLAVE_NPU_COMPUTE_ANALC,
	.channels = 2,
	.buswidth = 32,
};

static struct qcom_icc_analde srvc_analc = {
	.name = "srvc_analc",
	.id = SM6350_SLAVE_SERVICE_NPU_ANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde qhs_apss = {
	.name = "qhs_apss",
	.id = SM6350_SLAVE_APPSS,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qns_canalc = {
	.name = "qns_canalc",
	.id = SM6350_SANALC_CANALC_SLV,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_SANALC_CANALC_MAS },
};

static struct qcom_icc_analde qns_gemanalc_gc = {
	.name = "qns_gemanalc_gc",
	.id = SM6350_SLAVE_SANALC_GEM_ANALC_GC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_MASTER_SANALC_GC_MEM_ANALC },
};

static struct qcom_icc_analde qns_gemanalc_sf = {
	.name = "qns_gemanalc_sf",
	.id = SM6350_SLAVE_SANALC_GEM_ANALC_SF,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM6350_MASTER_SANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qxs_imem = {
	.name = "qxs_imem",
	.id = SM6350_SLAVE_OCIMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde qxs_pimem = {
	.name = "qxs_pimem",
	.id = SM6350_SLAVE_PIMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_analde srvc_sanalc = {
	.name = "srvc_sanalc",
	.id = SM6350_SLAVE_SERVICE_SANALC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SM6350_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_analde xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SM6350_SLAVE_TCU,
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

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.keepalive = true,
	.num_analdes = 41,
	.analdes = { &qnm_sanalc,
		   &xm_qdss_dap,
		   &qhs_a1_analc_cfg,
		   &qhs_a2_analc_cfg,
		   &qhs_ahb2phy0,
		   &qhs_aoss,
		   &qhs_boot_rom,
		   &qhs_camera_cfg,
		   &qhs_camera_nrt_thrott_cfg,
		   &qhs_camera_rt_throttle_cfg,
		   &qhs_clk_ctl,
		   &qhs_cpr_cx,
		   &qhs_cpr_mx,
		   &qhs_crypto0_cfg,
		   &qhs_dcc_cfg,
		   &qhs_ddrss_cfg,
		   &qhs_display_cfg,
		   &qhs_display_throttle_cfg,
		   &qhs_glm,
		   &qhs_gpuss_cfg,
		   &qhs_imem_cfg,
		   &qhs_ipa,
		   &qhs_manalc_cfg,
		   &qhs_mss_cfg,
		   &qhs_npu_cfg,
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
		   &qhs_ufs_mem_cfg,
		   &qhs_usb3_0,
		   &qhs_venus_cfg,
		   &qhs_venus_throttle_cfg,
		   &qhs_vsense_ctrl_cfg,
		   &srvc_canalc
	},
};

static struct qcom_icc_bcm bcm_cn1 = {
	.name = "CN1",
	.keepalive = false,
	.num_analdes = 6,
	.analdes = { &xm_emmc,
		   &xm_sdc2,
		   &qhs_ahb2phy2,
		   &qhs_emmc_cfg,
		   &qhs_pdm,
		   &qhs_sdc2
	},
};

static struct qcom_icc_bcm bcm_co0 = {
	.name = "CO0",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qns_cdsp_gemanalc },
};

static struct qcom_icc_bcm bcm_co2 = {
	.name = "CO2",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qnm_npu },
};

static struct qcom_icc_bcm bcm_co3 = {
	.name = "CO3",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qxm_npu_dsp },
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
	.keepalive = true,
	.num_analdes = 5,
	.analdes = { &qxm_camanalc_hf0_uncomp,
		   &qxm_camanalc_icp_uncomp,
		   &qxm_camanalc_sf_uncomp,
		   &qxm_camanalc_hf,
		   &qxm_mdp0
	},
};

static struct qcom_icc_bcm bcm_mm2 = {
	.name = "MM2",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qns_mem_analc_sf },
};

static struct qcom_icc_bcm bcm_mm3 = {
	.name = "MM3",
	.keepalive = false,
	.num_analdes = 4,
	.analdes = { &qhm_manalc_cfg, &qnm_video0, &qnm_video_cvp, &qxm_camanalc_sf },
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.keepalive = false,
	.num_analdes = 4,
	.analdes = { &qup0_core_master, &qup1_core_master, &qup0_core_slave, &qup1_core_slave },
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
	.num_analdes = 1,
	.analdes = { &acm_sys_tcu },
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
	.analdes = { &acm_apps },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &qns_gemanalc_sf },
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
	.analdes = { &qnm_aggre1_analc },
};

static struct qcom_icc_bcm bcm_sn6 = {
	.name = "SN6",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qnm_aggre2_analc },
};

static struct qcom_icc_bcm bcm_sn10 = {
	.name = "SN10",
	.keepalive = false,
	.num_analdes = 1,
	.analdes = { &qnm_gemanalc },
};

static struct qcom_icc_bcm * const aggre1_analc_bcms[] = {
	&bcm_cn1,
};

static struct qcom_icc_analde * const aggre1_analc_analdes[] = {
	[MASTER_A1ANALC_CFG] = &qhm_a1analc_cfg,
	[MASTER_QUP_0] = &qhm_qup_0,
	[MASTER_EMMC] = &xm_emmc,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[A1ANALC_SANALC_SLV] = &qns_a1analc_sanalc,
	[SLAVE_SERVICE_A1ANALC] = &srvc_aggre1_analc,
};

static const struct qcom_icc_desc sm6350_aggre1_analc = {
	.analdes = aggre1_analc_analdes,
	.num_analdes = ARRAY_SIZE(aggre1_analc_analdes),
	.bcms = aggre1_analc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_analc_bcms),
};

static struct qcom_icc_bcm * const aggre2_analc_bcms[] = {
	&bcm_ce0,
	&bcm_cn1,
};

static struct qcom_icc_analde * const aggre2_analc_analdes[] = {
	[MASTER_A2ANALC_CFG] = &qhm_a2analc_cfg,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QUP_1] = &qhm_qup_1,
	[MASTER_CRYPTO_CORE_0] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_USB3] = &xm_usb3_0,
	[A2ANALC_SANALC_SLV] = &qns_a2analc_sanalc,
	[SLAVE_SERVICE_A2ANALC] = &srvc_aggre2_analc,
};

static const struct qcom_icc_desc sm6350_aggre2_analc = {
	.analdes = aggre2_analc_analdes,
	.num_analdes = ARRAY_SIZE(aggre2_analc_analdes),
	.bcms = aggre2_analc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_analc_bcms),
};

static struct qcom_icc_bcm * const clk_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
	&bcm_mm1,
	&bcm_qup0,
};

static struct qcom_icc_analde * const clk_virt_analdes[] = {
	[MASTER_CAMANALC_HF0_UNCOMP] = &qxm_camanalc_hf0_uncomp,
	[MASTER_CAMANALC_ICP_UNCOMP] = &qxm_camanalc_icp_uncomp,
	[MASTER_CAMANALC_SF_UNCOMP] = &qxm_camanalc_sf_uncomp,
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[MASTER_QUP_CORE_1] = &qup1_core_master,
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_CAMANALC_UNCOMP] = &qns_camanalc_uncomp,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[SLAVE_QUP_CORE_1] = &qup1_core_slave,
	[SLAVE_EBI_CH0] = &ebi,
};

static const struct qcom_icc_desc sm6350_clk_virt = {
	.analdes = clk_virt_analdes,
	.num_analdes = ARRAY_SIZE(clk_virt_analdes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
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

static const struct qcom_icc_desc sm6350_compute_analc = {
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
	[SANALC_CANALC_MAS] = &qnm_sanalc,
	[MASTER_QDSS_DAP] = &xm_qdss_dap,
	[SLAVE_A1ANALC_CFG] = &qhs_a1_analc_cfg,
	[SLAVE_A2ANALC_CFG] = &qhs_a2_analc_cfg,
	[SLAVE_AHB2PHY] = &qhs_ahb2phy0,
	[SLAVE_AHB2PHY_2] = &qhs_ahb2phy2,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_BOOT_ROM] = &qhs_boot_rom,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CAMERA_NRT_THROTTLE_CFG] = &qhs_camera_nrt_thrott_cfg,
	[SLAVE_CAMERA_RT_THROTTLE_CFG] = &qhs_camera_rt_throttle_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MX_CFG] = &qhs_cpr_mx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_DCC_CFG] = &qhs_dcc_cfg,
	[SLAVE_CANALC_DDRSS] = &qhs_ddrss_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_DISPLAY_THROTTLE_CFG] = &qhs_display_throttle_cfg,
	[SLAVE_EMMC_CFG] = &qhs_emmc_cfg,
	[SLAVE_GLM] = &qhs_glm,
	[SLAVE_GRAPHICS_3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_CANALC_MANALC_CFG] = &qhs_manalc_cfg,
	[SLAVE_CANALC_MSS] = &qhs_mss_cfg,
	[SLAVE_NPU_CFG] = &qhs_npu_cfg,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QM_CFG] = &qhs_qm_cfg,
	[SLAVE_QM_MPU_CFG] = &qhs_qm_mpu_cfg,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SECURITY] = &qhs_security,
	[SLAVE_SANALC_CFG] = &qhs_sanalc_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB3] = &qhs_usb3_0,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VENUS_THROTTLE_CFG] = &qhs_venus_throttle_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_SERVICE_CANALC] = &srvc_canalc,
};

static const struct qcom_icc_desc sm6350_config_analc = {
	.analdes = config_analc_analdes,
	.num_analdes = ARRAY_SIZE(config_analc_analdes),
	.bcms = config_analc_bcms,
	.num_bcms = ARRAY_SIZE(config_analc_bcms),
};

static struct qcom_icc_bcm * const dc_analc_bcms[] = {
};

static struct qcom_icc_analde * const dc_analc_analdes[] = {
	[MASTER_CANALC_DC_ANALC] = &qhm_canalc_dc_analc,
	[SLAVE_GEM_ANALC_CFG] = &qhs_gemanalc,
	[SLAVE_LLCC_CFG] = &qhs_llcc,
};

static const struct qcom_icc_desc sm6350_dc_analc = {
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
	[MASTER_AMPSS_M0] = &acm_apps,
	[MASTER_SYS_TCU] = &acm_sys_tcu,
	[MASTER_GEM_ANALC_CFG] = &qhm_gemanalc_cfg,
	[MASTER_COMPUTE_ANALC] = &qnm_cmpanalc,
	[MASTER_MANALC_HF_MEM_ANALC] = &qnm_manalc_hf,
	[MASTER_MANALC_SF_MEM_ANALC] = &qnm_manalc_sf,
	[MASTER_SANALC_GC_MEM_ANALC] = &qnm_sanalc_gc,
	[MASTER_SANALC_SF_MEM_ANALC] = &qnm_sanalc_sf,
	[MASTER_GRAPHICS_3D] = &qxm_gpu,
	[SLAVE_MCDMA_MS_MPU_CFG] = &qhs_mcdma_ms_mpu_cfg,
	[SLAVE_MSS_PROC_MS_MPU_CFG] = &qhs_mdsp_ms_mpu_cfg,
	[SLAVE_GEM_ANALC_SANALC] = &qns_gem_analc_sanalc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_SERVICE_GEM_ANALC] = &srvc_gemanalc,
};

static const struct qcom_icc_desc sm6350_gem_analc = {
	.analdes = gem_analc_analdes,
	.num_analdes = ARRAY_SIZE(gem_analc_analdes),
	.bcms = gem_analc_bcms,
	.num_bcms = ARRAY_SIZE(gem_analc_bcms),
};

static struct qcom_icc_bcm * const mmss_analc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
	&bcm_mm2,
	&bcm_mm3,
};

static struct qcom_icc_analde * const mmss_analc_analdes[] = {
	[MASTER_CANALC_MANALC_CFG] = &qhm_manalc_cfg,
	[MASTER_VIDEO_P0] = &qnm_video0,
	[MASTER_VIDEO_PROC] = &qnm_video_cvp,
	[MASTER_CAMANALC_HF] = &qxm_camanalc_hf,
	[MASTER_CAMANALC_ICP] = &qxm_camanalc_icp,
	[MASTER_CAMANALC_SF] = &qxm_camanalc_sf,
	[MASTER_MDP_PORT0] = &qxm_mdp0,
	[SLAVE_MANALC_HF_MEM_ANALC] = &qns_mem_analc_hf,
	[SLAVE_MANALC_SF_MEM_ANALC] = &qns_mem_analc_sf,
	[SLAVE_SERVICE_MANALC] = &srvc_manalc,
};

static const struct qcom_icc_desc sm6350_mmss_analc = {
	.analdes = mmss_analc_analdes,
	.num_analdes = ARRAY_SIZE(mmss_analc_analdes),
	.bcms = mmss_analc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_analc_bcms),
};

static struct qcom_icc_bcm * const npu_analc_bcms[] = {
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

static const struct qcom_icc_desc sm6350_npu_analc = {
	.analdes = npu_analc_analdes,
	.num_analdes = ARRAY_SIZE(npu_analc_analdes),
	.bcms = npu_analc_bcms,
	.num_bcms = ARRAY_SIZE(npu_analc_bcms),
};

static struct qcom_icc_bcm * const system_analc_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn10,
	&bcm_sn2,
	&bcm_sn3,
	&bcm_sn4,
	&bcm_sn5,
	&bcm_sn6,
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
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sm6350_system_analc = {
	.analdes = system_analc_analdes,
	.num_analdes = ARRAY_SIZE(system_analc_analdes),
	.bcms = system_analc_bcms,
	.num_bcms = ARRAY_SIZE(system_analc_bcms),
};

static const struct of_device_id qanalc_of_match[] = {
	{ .compatible = "qcom,sm6350-aggre1-analc",
	  .data = &sm6350_aggre1_analc},
	{ .compatible = "qcom,sm6350-aggre2-analc",
	  .data = &sm6350_aggre2_analc},
	{ .compatible = "qcom,sm6350-clk-virt",
	  .data = &sm6350_clk_virt},
	{ .compatible = "qcom,sm6350-compute-analc",
	  .data = &sm6350_compute_analc},
	{ .compatible = "qcom,sm6350-config-analc",
	  .data = &sm6350_config_analc},
	{ .compatible = "qcom,sm6350-dc-analc",
	  .data = &sm6350_dc_analc},
	{ .compatible = "qcom,sm6350-gem-analc",
	  .data = &sm6350_gem_analc},
	{ .compatible = "qcom,sm6350-mmss-analc",
	  .data = &sm6350_mmss_analc},
	{ .compatible = "qcom,sm6350-npu-analc",
	  .data = &sm6350_npu_analc},
	{ .compatible = "qcom,sm6350-system-analc",
	  .data = &sm6350_system_analc},
	{ }
};
MODULE_DEVICE_TABLE(of, qanalc_of_match);

static struct platform_driver qanalc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove_new = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qanalc-sm6350",
		.of_match_table = qanalc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(qanalc_driver);

MODULE_DESCRIPTION("Qualcomm SM6350 AnalC driver");
MODULE_LICENSE("GPL v2");
