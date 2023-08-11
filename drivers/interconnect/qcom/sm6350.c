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

static struct qcom_icc_node qhm_a1noc_cfg = {
	.name = "qhm_a1noc_cfg",
	.id = SM6350_MASTER_A1NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_SLAVE_SERVICE_A1NOC },
};

static struct qcom_icc_node qhm_qup_0 = {
	.name = "qhm_qup_0",
	.id = SM6350_MASTER_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_A1NOC_SNOC_SLV },
};

static struct qcom_icc_node xm_emmc = {
	.name = "xm_emmc",
	.id = SM6350_MASTER_EMMC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_A1NOC_SNOC_SLV },
};

static struct qcom_icc_node xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.id = SM6350_MASTER_UFS_MEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_A1NOC_SNOC_SLV },
};

static struct qcom_icc_node qhm_a2noc_cfg = {
	.name = "qhm_a2noc_cfg",
	.id = SM6350_MASTER_A2NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_SLAVE_SERVICE_A2NOC },
};

static struct qcom_icc_node qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = SM6350_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_A2NOC_SNOC_SLV },
};

static struct qcom_icc_node qhm_qup_1 = {
	.name = "qhm_qup_1",
	.id = SM6350_MASTER_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_A2NOC_SNOC_SLV },
};

static struct qcom_icc_node qxm_crypto = {
	.name = "qxm_crypto",
	.id = SM6350_MASTER_CRYPTO_CORE_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_A2NOC_SNOC_SLV },
};

static struct qcom_icc_node qxm_ipa = {
	.name = "qxm_ipa",
	.id = SM6350_MASTER_IPA,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_A2NOC_SNOC_SLV },
};

static struct qcom_icc_node xm_qdss_etr = {
	.name = "xm_qdss_etr",
	.id = SM6350_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_A2NOC_SNOC_SLV },
};

static struct qcom_icc_node xm_sdc2 = {
	.name = "xm_sdc2",
	.id = SM6350_MASTER_SDCC_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_A2NOC_SNOC_SLV },
};

static struct qcom_icc_node xm_usb3_0 = {
	.name = "xm_usb3_0",
	.id = SM6350_MASTER_USB3,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_A2NOC_SNOC_SLV },
};

static struct qcom_icc_node qxm_camnoc_hf0_uncomp = {
	.name = "qxm_camnoc_hf0_uncomp",
	.id = SM6350_MASTER_CAMNOC_HF0_UNCOMP,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_SLAVE_CAMNOC_UNCOMP },
};

static struct qcom_icc_node qxm_camnoc_icp_uncomp = {
	.name = "qxm_camnoc_icp_uncomp",
	.id = SM6350_MASTER_CAMNOC_ICP_UNCOMP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_SLAVE_CAMNOC_UNCOMP },
};

static struct qcom_icc_node qxm_camnoc_sf_uncomp = {
	.name = "qxm_camnoc_sf_uncomp",
	.id = SM6350_MASTER_CAMNOC_SF_UNCOMP,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_SLAVE_CAMNOC_UNCOMP },
};

static struct qcom_icc_node qup0_core_master = {
	.name = "qup0_core_master",
	.id = SM6350_MASTER_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_SLAVE_QUP_CORE_0 },
};

static struct qcom_icc_node qup1_core_master = {
	.name = "qup1_core_master",
	.id = SM6350_MASTER_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_SLAVE_QUP_CORE_1 },
};

static struct qcom_icc_node qnm_npu = {
	.name = "qnm_npu",
	.id = SM6350_MASTER_NPU,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_SLAVE_CDSP_GEM_NOC },
};

static struct qcom_icc_node qxm_npu_dsp = {
	.name = "qxm_npu_dsp",
	.id = SM6350_MASTER_NPU_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_SLAVE_CDSP_GEM_NOC },
};

static struct qcom_icc_node qnm_snoc = {
	.name = "qnm_snoc",
	.id = SM6350_SNOC_CNOC_MAS,
	.channels = 1,
	.buswidth = 8,
	.num_links = 42,
	.links = { SM6350_SLAVE_CAMERA_CFG,
		   SM6350_SLAVE_SDCC_2,
		   SM6350_SLAVE_CNOC_MNOC_CFG,
		   SM6350_SLAVE_UFS_MEM_CFG,
		   SM6350_SLAVE_QM_CFG,
		   SM6350_SLAVE_SNOC_CFG,
		   SM6350_SLAVE_QM_MPU_CFG,
		   SM6350_SLAVE_GLM,
		   SM6350_SLAVE_PDM,
		   SM6350_SLAVE_CAMERA_NRT_THROTTLE_CFG,
		   SM6350_SLAVE_A2NOC_CFG,
		   SM6350_SLAVE_QDSS_CFG,
		   SM6350_SLAVE_VSENSE_CTRL_CFG,
		   SM6350_SLAVE_CAMERA_RT_THROTTLE_CFG,
		   SM6350_SLAVE_DISPLAY_CFG,
		   SM6350_SLAVE_TCSR,
		   SM6350_SLAVE_DCC_CFG,
		   SM6350_SLAVE_CNOC_DDRSS,
		   SM6350_SLAVE_DISPLAY_THROTTLE_CFG,
		   SM6350_SLAVE_NPU_CFG,
		   SM6350_SLAVE_AHB2PHY,
		   SM6350_SLAVE_GRAPHICS_3D_CFG,
		   SM6350_SLAVE_BOOT_ROM,
		   SM6350_SLAVE_VENUS_CFG,
		   SM6350_SLAVE_IPA_CFG,
		   SM6350_SLAVE_SECURITY,
		   SM6350_SLAVE_IMEM_CFG,
		   SM6350_SLAVE_CNOC_MSS,
		   SM6350_SLAVE_SERVICE_CNOC,
		   SM6350_SLAVE_USB3,
		   SM6350_SLAVE_VENUS_THROTTLE_CFG,
		   SM6350_SLAVE_RBCPR_CX_CFG,
		   SM6350_SLAVE_A1NOC_CFG,
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

static struct qcom_icc_node xm_qdss_dap = {
	.name = "xm_qdss_dap",
	.id = SM6350_MASTER_QDSS_DAP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 42,
	.links = { SM6350_SLAVE_CAMERA_CFG,
		   SM6350_SLAVE_SDCC_2,
		   SM6350_SLAVE_CNOC_MNOC_CFG,
		   SM6350_SLAVE_UFS_MEM_CFG,
		   SM6350_SLAVE_QM_CFG,
		   SM6350_SLAVE_SNOC_CFG,
		   SM6350_SLAVE_QM_MPU_CFG,
		   SM6350_SLAVE_GLM,
		   SM6350_SLAVE_PDM,
		   SM6350_SLAVE_CAMERA_NRT_THROTTLE_CFG,
		   SM6350_SLAVE_A2NOC_CFG,
		   SM6350_SLAVE_QDSS_CFG,
		   SM6350_SLAVE_VSENSE_CTRL_CFG,
		   SM6350_SLAVE_CAMERA_RT_THROTTLE_CFG,
		   SM6350_SLAVE_DISPLAY_CFG,
		   SM6350_SLAVE_TCSR,
		   SM6350_SLAVE_DCC_CFG,
		   SM6350_SLAVE_CNOC_DDRSS,
		   SM6350_SLAVE_DISPLAY_THROTTLE_CFG,
		   SM6350_SLAVE_NPU_CFG,
		   SM6350_SLAVE_AHB2PHY,
		   SM6350_SLAVE_GRAPHICS_3D_CFG,
		   SM6350_SLAVE_BOOT_ROM,
		   SM6350_SLAVE_VENUS_CFG,
		   SM6350_SLAVE_IPA_CFG,
		   SM6350_SLAVE_SECURITY,
		   SM6350_SLAVE_IMEM_CFG,
		   SM6350_SLAVE_CNOC_MSS,
		   SM6350_SLAVE_SERVICE_CNOC,
		   SM6350_SLAVE_USB3,
		   SM6350_SLAVE_VENUS_THROTTLE_CFG,
		   SM6350_SLAVE_RBCPR_CX_CFG,
		   SM6350_SLAVE_A1NOC_CFG,
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

static struct qcom_icc_node qhm_cnoc_dc_noc = {
	.name = "qhm_cnoc_dc_noc",
	.id = SM6350_MASTER_CNOC_DC_NOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.links = { SM6350_SLAVE_LLCC_CFG,
		   SM6350_SLAVE_GEM_NOC_CFG
	},
};

static struct qcom_icc_node acm_apps = {
	.name = "acm_apps",
	.id = SM6350_MASTER_AMPSS_M0,
	.channels = 1,
	.buswidth = 16,
	.num_links = 2,
	.links = { SM6350_SLAVE_LLCC,
		   SM6350_SLAVE_GEM_NOC_SNOC
	},
};

static struct qcom_icc_node acm_sys_tcu = {
	.name = "acm_sys_tcu",
	.id = SM6350_MASTER_SYS_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM6350_SLAVE_LLCC,
		   SM6350_SLAVE_GEM_NOC_SNOC
	},
};

static struct qcom_icc_node qhm_gemnoc_cfg = {
	.name = "qhm_gemnoc_cfg",
	.id = SM6350_MASTER_GEM_NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 3,
	.links = { SM6350_SLAVE_MCDMA_MS_MPU_CFG,
		   SM6350_SLAVE_SERVICE_GEM_NOC,
		   SM6350_SLAVE_MSS_PROC_MS_MPU_CFG
	},
};

static struct qcom_icc_node qnm_cmpnoc = {
	.name = "qnm_cmpnoc",
	.id = SM6350_MASTER_COMPUTE_NOC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM6350_SLAVE_LLCC,
		   SM6350_SLAVE_GEM_NOC_SNOC
	},
};

static struct qcom_icc_node qnm_mnoc_hf = {
	.name = "qnm_mnoc_hf",
	.id = SM6350_MASTER_MNOC_HF_MEM_NOC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM6350_SLAVE_LLCC,
		   SM6350_SLAVE_GEM_NOC_SNOC
	},
};

static struct qcom_icc_node qnm_mnoc_sf = {
	.name = "qnm_mnoc_sf",
	.id = SM6350_MASTER_MNOC_SF_MEM_NOC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM6350_SLAVE_LLCC,
		   SM6350_SLAVE_GEM_NOC_SNOC
	},
};

static struct qcom_icc_node qnm_snoc_gc = {
	.name = "qnm_snoc_gc",
	.id = SM6350_MASTER_SNOC_GC_MEM_NOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_snoc_sf = {
	.name = "qnm_snoc_sf",
	.id = SM6350_MASTER_SNOC_SF_MEM_NOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM6350_SLAVE_LLCC },
};

static struct qcom_icc_node qxm_gpu = {
	.name = "qxm_gpu",
	.id = SM6350_MASTER_GRAPHICS_3D,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SM6350_SLAVE_LLCC,
		   SM6350_SLAVE_GEM_NOC_SNOC
	},
};

static struct qcom_icc_node llcc_mc = {
	.name = "llcc_mc",
	.id = SM6350_MASTER_LLCC,
	.channels = 2,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_SLAVE_EBI_CH0 },
};

static struct qcom_icc_node qhm_mnoc_cfg = {
	.name = "qhm_mnoc_cfg",
	.id = SM6350_MASTER_CNOC_MNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_SLAVE_SERVICE_MNOC },
};

static struct qcom_icc_node qnm_video0 = {
	.name = "qnm_video0",
	.id = SM6350_MASTER_VIDEO_P0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qnm_video_cvp = {
	.name = "qnm_video_cvp",
	.id = SM6350_MASTER_VIDEO_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qxm_camnoc_hf = {
	.name = "qxm_camnoc_hf",
	.id = SM6350_MASTER_CAMNOC_HF,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qxm_camnoc_icp = {
	.name = "qxm_camnoc_icp",
	.id = SM6350_MASTER_CAMNOC_ICP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qxm_camnoc_sf = {
	.name = "qxm_camnoc_sf",
	.id = SM6350_MASTER_CAMNOC_SF,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qxm_mdp0 = {
	.name = "qxm_mdp0",
	.id = SM6350_MASTER_MDP_PORT0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node amm_npu_sys = {
	.name = "amm_npu_sys",
	.id = SM6350_MASTER_NPU_SYS,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_SLAVE_NPU_COMPUTE_NOC },
};

static struct qcom_icc_node qhm_npu_cfg = {
	.name = "qhm_npu_cfg",
	.id = SM6350_MASTER_NPU_NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 8,
	.links = { SM6350_SLAVE_SERVICE_NPU_NOC,
		   SM6350_SLAVE_ISENSE_CFG,
		   SM6350_SLAVE_NPU_LLM_CFG,
		   SM6350_SLAVE_NPU_INT_DMA_BWMON_CFG,
		   SM6350_SLAVE_NPU_CP,
		   SM6350_SLAVE_NPU_TCM,
		   SM6350_SLAVE_NPU_CAL_DP0,
		   SM6350_SLAVE_NPU_DPM
	},
};

static struct qcom_icc_node qhm_snoc_cfg = {
	.name = "qhm_snoc_cfg",
	.id = SM6350_MASTER_SNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_SLAVE_SERVICE_SNOC },
};

static struct qcom_icc_node qnm_aggre1_noc = {
	.name = "qnm_aggre1_noc",
	.id = SM6350_A1NOC_SNOC_MAS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 6,
	.links = { SM6350_SLAVE_SNOC_GEM_NOC_SF,
		   SM6350_SLAVE_PIMEM,
		   SM6350_SLAVE_OCIMEM,
		   SM6350_SLAVE_APPSS,
		   SM6350_SNOC_CNOC_SLV,
		   SM6350_SLAVE_QDSS_STM
	},
};

static struct qcom_icc_node qnm_aggre2_noc = {
	.name = "qnm_aggre2_noc",
	.id = SM6350_A2NOC_SNOC_MAS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 7,
	.links = { SM6350_SLAVE_SNOC_GEM_NOC_SF,
		   SM6350_SLAVE_PIMEM,
		   SM6350_SLAVE_OCIMEM,
		   SM6350_SLAVE_APPSS,
		   SM6350_SNOC_CNOC_SLV,
		   SM6350_SLAVE_TCU,
		   SM6350_SLAVE_QDSS_STM
	},
};

static struct qcom_icc_node qnm_gemnoc = {
	.name = "qnm_gemnoc",
	.id = SM6350_MASTER_GEM_NOC_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 6,
	.links = { SM6350_SLAVE_PIMEM,
		   SM6350_SLAVE_OCIMEM,
		   SM6350_SLAVE_APPSS,
		   SM6350_SNOC_CNOC_SLV,
		   SM6350_SLAVE_TCU,
		   SM6350_SLAVE_QDSS_STM
	},
};

static struct qcom_icc_node qxm_pimem = {
	.name = "qxm_pimem",
	.id = SM6350_MASTER_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SM6350_SLAVE_SNOC_GEM_NOC_GC,
		   SM6350_SLAVE_OCIMEM
	},
};

static struct qcom_icc_node xm_gic = {
	.name = "xm_gic",
	.id = SM6350_MASTER_GIC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_SLAVE_SNOC_GEM_NOC_GC },
};

static struct qcom_icc_node qns_a1noc_snoc = {
	.name = "qns_a1noc_snoc",
	.id = SM6350_A1NOC_SNOC_SLV,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM6350_A1NOC_SNOC_MAS },
};

static struct qcom_icc_node srvc_aggre1_noc = {
	.name = "srvc_aggre1_noc",
	.id = SM6350_SLAVE_SERVICE_A1NOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_a2noc_snoc = {
	.name = "qns_a2noc_snoc",
	.id = SM6350_A2NOC_SNOC_SLV,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM6350_A2NOC_SNOC_MAS },
};

static struct qcom_icc_node srvc_aggre2_noc = {
	.name = "srvc_aggre2_noc",
	.id = SM6350_SLAVE_SERVICE_A2NOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_camnoc_uncomp = {
	.name = "qns_camnoc_uncomp",
	.id = SM6350_SLAVE_CAMNOC_UNCOMP,
	.channels = 1,
	.buswidth = 32,
};

static struct qcom_icc_node qup0_core_slave = {
	.name = "qup0_core_slave",
	.id = SM6350_SLAVE_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qup1_core_slave = {
	.name = "qup1_core_slave",
	.id = SM6350_SLAVE_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_cdsp_gemnoc = {
	.name = "qns_cdsp_gemnoc",
	.id = SM6350_SLAVE_CDSP_GEM_NOC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_MASTER_COMPUTE_NOC },
};

static struct qcom_icc_node qhs_a1_noc_cfg = {
	.name = "qhs_a1_noc_cfg",
	.id = SM6350_SLAVE_A1NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_MASTER_A1NOC_CFG },
};

static struct qcom_icc_node qhs_a2_noc_cfg = {
	.name = "qhs_a2_noc_cfg",
	.id = SM6350_SLAVE_A2NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_MASTER_A2NOC_CFG },
};

static struct qcom_icc_node qhs_ahb2phy0 = {
	.name = "qhs_ahb2phy0",
	.id = SM6350_SLAVE_AHB2PHY,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ahb2phy2 = {
	.name = "qhs_ahb2phy2",
	.id = SM6350_SLAVE_AHB2PHY_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_aoss = {
	.name = "qhs_aoss",
	.id = SM6350_SLAVE_AOSS,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_boot_rom = {
	.name = "qhs_boot_rom",
	.id = SM6350_SLAVE_BOOT_ROM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_camera_cfg = {
	.name = "qhs_camera_cfg",
	.id = SM6350_SLAVE_CAMERA_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_camera_nrt_thrott_cfg = {
	.name = "qhs_camera_nrt_thrott_cfg",
	.id = SM6350_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_camera_rt_throttle_cfg = {
	.name = "qhs_camera_rt_throttle_cfg",
	.id = SM6350_SLAVE_CAMERA_RT_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SM6350_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = SM6350_SLAVE_RBCPR_CX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_cpr_mx = {
	.name = "qhs_cpr_mx",
	.id = SM6350_SLAVE_RBCPR_MX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = SM6350_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_dcc_cfg = {
	.name = "qhs_dcc_cfg",
	.id = SM6350_SLAVE_DCC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ddrss_cfg = {
	.name = "qhs_ddrss_cfg",
	.id = SM6350_SLAVE_CNOC_DDRSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_MASTER_CNOC_DC_NOC },
};

static struct qcom_icc_node qhs_display_cfg = {
	.name = "qhs_display_cfg",
	.id = SM6350_SLAVE_DISPLAY_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_display_throttle_cfg = {
	.name = "qhs_display_throttle_cfg",
	.id = SM6350_SLAVE_DISPLAY_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_emmc_cfg = {
	.name = "qhs_emmc_cfg",
	.id = SM6350_SLAVE_EMMC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_glm = {
	.name = "qhs_glm",
	.id = SM6350_SLAVE_GLM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.id = SM6350_SLAVE_GRAPHICS_3D_CFG,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SM6350_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ipa = {
	.name = "qhs_ipa",
	.id = SM6350_SLAVE_IPA_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_mnoc_cfg = {
	.name = "qhs_mnoc_cfg",
	.id = SM6350_SLAVE_CNOC_MNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_MASTER_CNOC_MNOC_CFG },
};

static struct qcom_icc_node qhs_mss_cfg = {
	.name = "qhs_mss_cfg",
	.id = SM6350_SLAVE_CNOC_MSS,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_npu_cfg = {
	.name = "qhs_npu_cfg",
	.id = SM6350_SLAVE_NPU_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_MASTER_NPU_NOC_CFG },
};

static struct qcom_icc_node qhs_pdm = {
	.name = "qhs_pdm",
	.id = SM6350_SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = SM6350_SLAVE_PIMEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_prng = {
	.name = "qhs_prng",
	.id = SM6350_SLAVE_PRNG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SM6350_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qm_cfg = {
	.name = "qhs_qm_cfg",
	.id = SM6350_SLAVE_QM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qm_mpu_cfg = {
	.name = "qhs_qm_mpu_cfg",
	.id = SM6350_SLAVE_QM_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qup0 = {
	.name = "qhs_qup0",
	.id = SM6350_SLAVE_QUP_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qup1 = {
	.name = "qhs_qup1",
	.id = SM6350_SLAVE_QUP_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = SM6350_SLAVE_SDCC_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_security = {
	.name = "qhs_security",
	.id = SM6350_SLAVE_SECURITY,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_snoc_cfg = {
	.name = "qhs_snoc_cfg",
	.id = SM6350_SLAVE_SNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_MASTER_SNOC_CFG },
};

static struct qcom_icc_node qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SM6350_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.id = SM6350_SLAVE_UFS_MEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb3_0 = {
	.name = "qhs_usb3_0",
	.id = SM6350_SLAVE_USB3,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = SM6350_SLAVE_VENUS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_venus_throttle_cfg = {
	.name = "qhs_venus_throttle_cfg",
	.id = SM6350_SLAVE_VENUS_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.id = SM6350_SLAVE_VSENSE_CTRL_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_cnoc = {
	.name = "srvc_cnoc",
	.id = SM6350_SLAVE_SERVICE_CNOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_gemnoc = {
	.name = "qhs_gemnoc",
	.id = SM6350_SLAVE_GEM_NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SM6350_MASTER_GEM_NOC_CFG },
};

static struct qcom_icc_node qhs_llcc = {
	.name = "qhs_llcc",
	.id = SM6350_SLAVE_LLCC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_mcdma_ms_mpu_cfg = {
	.name = "qhs_mcdma_ms_mpu_cfg",
	.id = SM6350_SLAVE_MCDMA_MS_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_mdsp_ms_mpu_cfg = {
	.name = "qhs_mdsp_ms_mpu_cfg",
	.id = SM6350_SLAVE_MSS_PROC_MS_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_gem_noc_snoc = {
	.name = "qns_gem_noc_snoc",
	.id = SM6350_SLAVE_GEM_NOC_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_MASTER_GEM_NOC_SNOC },
};

static struct qcom_icc_node qns_llcc = {
	.name = "qns_llcc",
	.id = SM6350_SLAVE_LLCC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM6350_MASTER_LLCC },
};

static struct qcom_icc_node srvc_gemnoc = {
	.name = "srvc_gemnoc",
	.id = SM6350_SLAVE_SERVICE_GEM_NOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.id = SM6350_SLAVE_EBI_CH0,
	.channels = 2,
	.buswidth = 4,
};

static struct qcom_icc_node qns_mem_noc_hf = {
	.name = "qns_mem_noc_hf",
	.id = SM6350_SLAVE_MNOC_HF_MEM_NOC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_MASTER_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qns_mem_noc_sf = {
	.name = "qns_mem_noc_sf",
	.id = SM6350_SLAVE_MNOC_SF_MEM_NOC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SM6350_MASTER_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node srvc_mnoc = {
	.name = "srvc_mnoc",
	.id = SM6350_SLAVE_SERVICE_MNOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_cal_dp0 = {
	.name = "qhs_cal_dp0",
	.id = SM6350_SLAVE_NPU_CAL_DP0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_cp = {
	.name = "qhs_cp",
	.id = SM6350_SLAVE_NPU_CP,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_dma_bwmon = {
	.name = "qhs_dma_bwmon",
	.id = SM6350_SLAVE_NPU_INT_DMA_BWMON_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_dpm = {
	.name = "qhs_dpm",
	.id = SM6350_SLAVE_NPU_DPM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_isense = {
	.name = "qhs_isense",
	.id = SM6350_SLAVE_ISENSE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_llm = {
	.name = "qhs_llm",
	.id = SM6350_SLAVE_NPU_LLM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_tcm = {
	.name = "qhs_tcm",
	.id = SM6350_SLAVE_NPU_TCM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_npu_sys = {
	.name = "qns_npu_sys",
	.id = SM6350_SLAVE_NPU_COMPUTE_NOC,
	.channels = 2,
	.buswidth = 32,
};

static struct qcom_icc_node srvc_noc = {
	.name = "srvc_noc",
	.id = SM6350_SLAVE_SERVICE_NPU_NOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_apss = {
	.name = "qhs_apss",
	.id = SM6350_SLAVE_APPSS,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qns_cnoc = {
	.name = "qns_cnoc",
	.id = SM6350_SNOC_CNOC_SLV,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_SNOC_CNOC_MAS },
};

static struct qcom_icc_node qns_gemnoc_gc = {
	.name = "qns_gemnoc_gc",
	.id = SM6350_SLAVE_SNOC_GEM_NOC_GC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SM6350_MASTER_SNOC_GC_MEM_NOC },
};

static struct qcom_icc_node qns_gemnoc_sf = {
	.name = "qns_gemnoc_sf",
	.id = SM6350_SLAVE_SNOC_GEM_NOC_SF,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SM6350_MASTER_SNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qxs_imem = {
	.name = "qxs_imem",
	.id = SM6350_SLAVE_OCIMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qxs_pimem = {
	.name = "qxs_pimem",
	.id = SM6350_SLAVE_PIMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node srvc_snoc = {
	.name = "srvc_snoc",
	.id = SM6350_SLAVE_SERVICE_SNOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SM6350_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SM6350_SLAVE_TCU,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
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
	.num_nodes = 41,
	.nodes = { &qnm_snoc,
		   &xm_qdss_dap,
		   &qhs_a1_noc_cfg,
		   &qhs_a2_noc_cfg,
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
		   &qhs_mnoc_cfg,
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
		   &qhs_snoc_cfg,
		   &qhs_tcsr,
		   &qhs_ufs_mem_cfg,
		   &qhs_usb3_0,
		   &qhs_venus_cfg,
		   &qhs_venus_throttle_cfg,
		   &qhs_vsense_ctrl_cfg,
		   &srvc_cnoc
	},
};

static struct qcom_icc_bcm bcm_cn1 = {
	.name = "CN1",
	.keepalive = false,
	.num_nodes = 6,
	.nodes = { &xm_emmc,
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
	.num_nodes = 1,
	.nodes = { &qns_cdsp_gemnoc },
};

static struct qcom_icc_bcm bcm_co2 = {
	.name = "CO2",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qnm_npu },
};

static struct qcom_icc_bcm bcm_co3 = {
	.name = "CO3",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qxm_npu_dsp },
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
	.keepalive = true,
	.num_nodes = 5,
	.nodes = { &qxm_camnoc_hf0_uncomp,
		   &qxm_camnoc_icp_uncomp,
		   &qxm_camnoc_sf_uncomp,
		   &qxm_camnoc_hf,
		   &qxm_mdp0
	},
};

static struct qcom_icc_bcm bcm_mm2 = {
	.name = "MM2",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_bcm bcm_mm3 = {
	.name = "MM3",
	.keepalive = false,
	.num_nodes = 4,
	.nodes = { &qhm_mnoc_cfg, &qnm_video0, &qnm_video_cvp, &qxm_camnoc_sf },
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.keepalive = false,
	.num_nodes = 4,
	.nodes = { &qup0_core_master, &qup1_core_master, &qup0_core_slave, &qup1_core_slave },
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
	.num_nodes = 1,
	.nodes = { &acm_sys_tcu },
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
	.nodes = { &acm_apps },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qxs_imem },
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
	.nodes = { &qnm_aggre1_noc },
};

static struct qcom_icc_bcm bcm_sn6 = {
	.name = "SN6",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qnm_aggre2_noc },
};

static struct qcom_icc_bcm bcm_sn10 = {
	.name = "SN10",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qnm_gemnoc },
};

static struct qcom_icc_bcm * const aggre1_noc_bcms[] = {
	&bcm_cn1,
};

static struct qcom_icc_node * const aggre1_noc_nodes[] = {
	[MASTER_A1NOC_CFG] = &qhm_a1noc_cfg,
	[MASTER_QUP_0] = &qhm_qup_0,
	[MASTER_EMMC] = &xm_emmc,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[A1NOC_SNOC_SLV] = &qns_a1noc_snoc,
	[SLAVE_SERVICE_A1NOC] = &srvc_aggre1_noc,
};

static const struct qcom_icc_desc sm6350_aggre1_noc = {
	.nodes = aggre1_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre1_noc_nodes),
	.bcms = aggre1_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_noc_bcms),
};

static struct qcom_icc_bcm * const aggre2_noc_bcms[] = {
	&bcm_ce0,
	&bcm_cn1,
};

static struct qcom_icc_node * const aggre2_noc_nodes[] = {
	[MASTER_A2NOC_CFG] = &qhm_a2noc_cfg,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QUP_1] = &qhm_qup_1,
	[MASTER_CRYPTO_CORE_0] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_USB3] = &xm_usb3_0,
	[A2NOC_SNOC_SLV] = &qns_a2noc_snoc,
	[SLAVE_SERVICE_A2NOC] = &srvc_aggre2_noc,
};

static const struct qcom_icc_desc sm6350_aggre2_noc = {
	.nodes = aggre2_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre2_noc_nodes),
	.bcms = aggre2_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_noc_bcms),
};

static struct qcom_icc_bcm * const clk_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
	&bcm_mm1,
	&bcm_qup0,
};

static struct qcom_icc_node * const clk_virt_nodes[] = {
	[MASTER_CAMNOC_HF0_UNCOMP] = &qxm_camnoc_hf0_uncomp,
	[MASTER_CAMNOC_ICP_UNCOMP] = &qxm_camnoc_icp_uncomp,
	[MASTER_CAMNOC_SF_UNCOMP] = &qxm_camnoc_sf_uncomp,
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[MASTER_QUP_CORE_1] = &qup1_core_master,
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_CAMNOC_UNCOMP] = &qns_camnoc_uncomp,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[SLAVE_QUP_CORE_1] = &qup1_core_slave,
	[SLAVE_EBI_CH0] = &ebi,
};

static const struct qcom_icc_desc sm6350_clk_virt = {
	.nodes = clk_virt_nodes,
	.num_nodes = ARRAY_SIZE(clk_virt_nodes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
};

static struct qcom_icc_bcm * const compute_noc_bcms[] = {
	&bcm_co0,
	&bcm_co2,
	&bcm_co3,
};

static struct qcom_icc_node * const compute_noc_nodes[] = {
	[MASTER_NPU] = &qnm_npu,
	[MASTER_NPU_PROC] = &qxm_npu_dsp,
	[SLAVE_CDSP_GEM_NOC] = &qns_cdsp_gemnoc,
};

static const struct qcom_icc_desc sm6350_compute_noc = {
	.nodes = compute_noc_nodes,
	.num_nodes = ARRAY_SIZE(compute_noc_nodes),
	.bcms = compute_noc_bcms,
	.num_bcms = ARRAY_SIZE(compute_noc_bcms),
};

static struct qcom_icc_bcm * const config_noc_bcms[] = {
	&bcm_cn0,
	&bcm_cn1,
};

static struct qcom_icc_node * const config_noc_nodes[] = {
	[SNOC_CNOC_MAS] = &qnm_snoc,
	[MASTER_QDSS_DAP] = &xm_qdss_dap,
	[SLAVE_A1NOC_CFG] = &qhs_a1_noc_cfg,
	[SLAVE_A2NOC_CFG] = &qhs_a2_noc_cfg,
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
	[SLAVE_CNOC_DDRSS] = &qhs_ddrss_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_DISPLAY_THROTTLE_CFG] = &qhs_display_throttle_cfg,
	[SLAVE_EMMC_CFG] = &qhs_emmc_cfg,
	[SLAVE_GLM] = &qhs_glm,
	[SLAVE_GRAPHICS_3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_CNOC_MNOC_CFG] = &qhs_mnoc_cfg,
	[SLAVE_CNOC_MSS] = &qhs_mss_cfg,
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
	[SLAVE_SNOC_CFG] = &qhs_snoc_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB3] = &qhs_usb3_0,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VENUS_THROTTLE_CFG] = &qhs_venus_throttle_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_SERVICE_CNOC] = &srvc_cnoc,
};

static const struct qcom_icc_desc sm6350_config_noc = {
	.nodes = config_noc_nodes,
	.num_nodes = ARRAY_SIZE(config_noc_nodes),
	.bcms = config_noc_bcms,
	.num_bcms = ARRAY_SIZE(config_noc_bcms),
};

static struct qcom_icc_bcm * const dc_noc_bcms[] = {
};

static struct qcom_icc_node * const dc_noc_nodes[] = {
	[MASTER_CNOC_DC_NOC] = &qhm_cnoc_dc_noc,
	[SLAVE_GEM_NOC_CFG] = &qhs_gemnoc,
	[SLAVE_LLCC_CFG] = &qhs_llcc,
};

static const struct qcom_icc_desc sm6350_dc_noc = {
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
};

static struct qcom_icc_node * const gem_noc_nodes[] = {
	[MASTER_AMPSS_M0] = &acm_apps,
	[MASTER_SYS_TCU] = &acm_sys_tcu,
	[MASTER_GEM_NOC_CFG] = &qhm_gemnoc_cfg,
	[MASTER_COMPUTE_NOC] = &qnm_cmpnoc,
	[MASTER_MNOC_HF_MEM_NOC] = &qnm_mnoc_hf,
	[MASTER_MNOC_SF_MEM_NOC] = &qnm_mnoc_sf,
	[MASTER_SNOC_GC_MEM_NOC] = &qnm_snoc_gc,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[MASTER_GRAPHICS_3D] = &qxm_gpu,
	[SLAVE_MCDMA_MS_MPU_CFG] = &qhs_mcdma_ms_mpu_cfg,
	[SLAVE_MSS_PROC_MS_MPU_CFG] = &qhs_mdsp_ms_mpu_cfg,
	[SLAVE_GEM_NOC_SNOC] = &qns_gem_noc_snoc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_SERVICE_GEM_NOC] = &srvc_gemnoc,
};

static const struct qcom_icc_desc sm6350_gem_noc = {
	.nodes = gem_noc_nodes,
	.num_nodes = ARRAY_SIZE(gem_noc_nodes),
	.bcms = gem_noc_bcms,
	.num_bcms = ARRAY_SIZE(gem_noc_bcms),
};

static struct qcom_icc_bcm * const mmss_noc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
	&bcm_mm2,
	&bcm_mm3,
};

static struct qcom_icc_node * const mmss_noc_nodes[] = {
	[MASTER_CNOC_MNOC_CFG] = &qhm_mnoc_cfg,
	[MASTER_VIDEO_P0] = &qnm_video0,
	[MASTER_VIDEO_PROC] = &qnm_video_cvp,
	[MASTER_CAMNOC_HF] = &qxm_camnoc_hf,
	[MASTER_CAMNOC_ICP] = &qxm_camnoc_icp,
	[MASTER_CAMNOC_SF] = &qxm_camnoc_sf,
	[MASTER_MDP_PORT0] = &qxm_mdp0,
	[SLAVE_MNOC_HF_MEM_NOC] = &qns_mem_noc_hf,
	[SLAVE_MNOC_SF_MEM_NOC] = &qns_mem_noc_sf,
	[SLAVE_SERVICE_MNOC] = &srvc_mnoc,
};

static const struct qcom_icc_desc sm6350_mmss_noc = {
	.nodes = mmss_noc_nodes,
	.num_nodes = ARRAY_SIZE(mmss_noc_nodes),
	.bcms = mmss_noc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_noc_bcms),
};

static struct qcom_icc_bcm * const npu_noc_bcms[] = {
};

static struct qcom_icc_node * const npu_noc_nodes[] = {
	[MASTER_NPU_SYS] = &amm_npu_sys,
	[MASTER_NPU_NOC_CFG] = &qhm_npu_cfg,
	[SLAVE_NPU_CAL_DP0] = &qhs_cal_dp0,
	[SLAVE_NPU_CP] = &qhs_cp,
	[SLAVE_NPU_INT_DMA_BWMON_CFG] = &qhs_dma_bwmon,
	[SLAVE_NPU_DPM] = &qhs_dpm,
	[SLAVE_ISENSE_CFG] = &qhs_isense,
	[SLAVE_NPU_LLM_CFG] = &qhs_llm,
	[SLAVE_NPU_TCM] = &qhs_tcm,
	[SLAVE_NPU_COMPUTE_NOC] = &qns_npu_sys,
	[SLAVE_SERVICE_NPU_NOC] = &srvc_noc,
};

static const struct qcom_icc_desc sm6350_npu_noc = {
	.nodes = npu_noc_nodes,
	.num_nodes = ARRAY_SIZE(npu_noc_nodes),
	.bcms = npu_noc_bcms,
	.num_bcms = ARRAY_SIZE(npu_noc_bcms),
};

static struct qcom_icc_bcm * const system_noc_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn10,
	&bcm_sn2,
	&bcm_sn3,
	&bcm_sn4,
	&bcm_sn5,
	&bcm_sn6,
};

static struct qcom_icc_node * const system_noc_nodes[] = {
	[MASTER_SNOC_CFG] = &qhm_snoc_cfg,
	[A1NOC_SNOC_MAS] = &qnm_aggre1_noc,
	[A2NOC_SNOC_MAS] = &qnm_aggre2_noc,
	[MASTER_GEM_NOC_SNOC] = &qnm_gemnoc,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_GIC] = &xm_gic,
	[SLAVE_APPSS] = &qhs_apss,
	[SNOC_CNOC_SLV] = &qns_cnoc,
	[SLAVE_SNOC_GEM_NOC_GC] = &qns_gemnoc_gc,
	[SLAVE_SNOC_GEM_NOC_SF] = &qns_gemnoc_sf,
	[SLAVE_OCIMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SERVICE_SNOC] = &srvc_snoc,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sm6350_system_noc = {
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,sm6350-aggre1-noc",
	  .data = &sm6350_aggre1_noc},
	{ .compatible = "qcom,sm6350-aggre2-noc",
	  .data = &sm6350_aggre2_noc},
	{ .compatible = "qcom,sm6350-clk-virt",
	  .data = &sm6350_clk_virt},
	{ .compatible = "qcom,sm6350-compute-noc",
	  .data = &sm6350_compute_noc},
	{ .compatible = "qcom,sm6350-config-noc",
	  .data = &sm6350_config_noc},
	{ .compatible = "qcom,sm6350-dc-noc",
	  .data = &sm6350_dc_noc},
	{ .compatible = "qcom,sm6350-gem-noc",
	  .data = &sm6350_gem_noc},
	{ .compatible = "qcom,sm6350-mmss-noc",
	  .data = &sm6350_mmss_noc},
	{ .compatible = "qcom,sm6350-npu-noc",
	  .data = &sm6350_npu_noc},
	{ .compatible = "qcom,sm6350-system-noc",
	  .data = &sm6350_system_noc},
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-sm6350",
		.of_match_table = qnoc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(qnoc_driver);

MODULE_DESCRIPTION("Qualcomm SM6350 NoC driver");
MODULE_LICENSE("GPL v2");
