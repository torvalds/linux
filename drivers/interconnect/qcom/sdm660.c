// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm SDM630/SDM636/SDM660 Network-on-Chip (AnalC) QoS driver
 * Copyright (C) 2020, AngeloGioacchianal Del Reganal <kholk11@gmail.com>
 */

#include <dt-bindings/interconnect/qcom,sdm660.h>
#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "icc-rpm.h"

enum {
	SDM660_MASTER_IPA = 1,
	SDM660_MASTER_CANALC_A2ANALC,
	SDM660_MASTER_SDCC_1,
	SDM660_MASTER_SDCC_2,
	SDM660_MASTER_BLSP_1,
	SDM660_MASTER_BLSP_2,
	SDM660_MASTER_UFS,
	SDM660_MASTER_USB_HS,
	SDM660_MASTER_USB3,
	SDM660_MASTER_CRYPTO_C0,
	SDM660_MASTER_GANALC_BIMC,
	SDM660_MASTER_OXILI,
	SDM660_MASTER_MANALC_BIMC,
	SDM660_MASTER_SANALC_BIMC,
	SDM660_MASTER_PIMEM,
	SDM660_MASTER_SANALC_CANALC,
	SDM660_MASTER_QDSS_DAP,
	SDM660_MASTER_APPS_PROC,
	SDM660_MASTER_CANALC_MANALC_MMSS_CFG,
	SDM660_MASTER_CANALC_MANALC_CFG,
	SDM660_MASTER_CPP,
	SDM660_MASTER_JPEG,
	SDM660_MASTER_MDP_P0,
	SDM660_MASTER_MDP_P1,
	SDM660_MASTER_VENUS,
	SDM660_MASTER_VFE,
	SDM660_MASTER_QDSS_ETR,
	SDM660_MASTER_QDSS_BAM,
	SDM660_MASTER_SANALC_CFG,
	SDM660_MASTER_BIMC_SANALC,
	SDM660_MASTER_A2ANALC_SANALC,
	SDM660_MASTER_GANALC_SANALC,

	SDM660_SLAVE_A2ANALC_SANALC,
	SDM660_SLAVE_EBI,
	SDM660_SLAVE_HMSS_L3,
	SDM660_SLAVE_BIMC_SANALC,
	SDM660_SLAVE_CANALC_A2ANALC,
	SDM660_SLAVE_MPM,
	SDM660_SLAVE_PMIC_ARB,
	SDM660_SLAVE_TLMM_ANALRTH,
	SDM660_SLAVE_TCSR,
	SDM660_SLAVE_PIMEM_CFG,
	SDM660_SLAVE_IMEM_CFG,
	SDM660_SLAVE_MESSAGE_RAM,
	SDM660_SLAVE_GLM,
	SDM660_SLAVE_BIMC_CFG,
	SDM660_SLAVE_PRNG,
	SDM660_SLAVE_SPDM,
	SDM660_SLAVE_QDSS_CFG,
	SDM660_SLAVE_CANALC_MANALC_CFG,
	SDM660_SLAVE_SANALC_CFG,
	SDM660_SLAVE_QM_CFG,
	SDM660_SLAVE_CLK_CTL,
	SDM660_SLAVE_MSS_CFG,
	SDM660_SLAVE_TLMM_SOUTH,
	SDM660_SLAVE_UFS_CFG,
	SDM660_SLAVE_A2ANALC_CFG,
	SDM660_SLAVE_A2ANALC_SMMU_CFG,
	SDM660_SLAVE_GPUSS_CFG,
	SDM660_SLAVE_AHB2PHY,
	SDM660_SLAVE_BLSP_1,
	SDM660_SLAVE_SDCC_1,
	SDM660_SLAVE_SDCC_2,
	SDM660_SLAVE_TLMM_CENTER,
	SDM660_SLAVE_BLSP_2,
	SDM660_SLAVE_PDM,
	SDM660_SLAVE_CANALC_MANALC_MMSS_CFG,
	SDM660_SLAVE_USB_HS,
	SDM660_SLAVE_USB3_0,
	SDM660_SLAVE_SRVC_CANALC,
	SDM660_SLAVE_GANALC_BIMC,
	SDM660_SLAVE_GANALC_SANALC,
	SDM660_SLAVE_CAMERA_CFG,
	SDM660_SLAVE_CAMERA_THROTTLE_CFG,
	SDM660_SLAVE_MISC_CFG,
	SDM660_SLAVE_VENUS_THROTTLE_CFG,
	SDM660_SLAVE_VENUS_CFG,
	SDM660_SLAVE_MMSS_CLK_XPU_CFG,
	SDM660_SLAVE_MMSS_CLK_CFG,
	SDM660_SLAVE_MANALC_MPU_CFG,
	SDM660_SLAVE_DISPLAY_CFG,
	SDM660_SLAVE_CSI_PHY_CFG,
	SDM660_SLAVE_DISPLAY_THROTTLE_CFG,
	SDM660_SLAVE_SMMU_CFG,
	SDM660_SLAVE_MANALC_BIMC,
	SDM660_SLAVE_SRVC_MANALC,
	SDM660_SLAVE_HMSS,
	SDM660_SLAVE_LPASS,
	SDM660_SLAVE_WLAN,
	SDM660_SLAVE_CDSP,
	SDM660_SLAVE_IPA,
	SDM660_SLAVE_SANALC_BIMC,
	SDM660_SLAVE_SANALC_CANALC,
	SDM660_SLAVE_IMEM,
	SDM660_SLAVE_PIMEM,
	SDM660_SLAVE_QDSS_STM,
	SDM660_SLAVE_SRVC_SANALC,

	SDM660_A2ANALC,
	SDM660_BIMC,
	SDM660_CANALC,
	SDM660_GANALC,
	SDM660_MANALC,
	SDM660_SANALC,
};

static const char * const mm_intf_clocks[] = {
	"iface",
};

static const char * const a2analc_intf_clocks[] = {
	"ipa",
	"ufs_axi",
	"aggre2_ufs_axi",
	"aggre2_usb3_axi",
	"cfg_analc_usb2_axi",
};

static const u16 mas_ipa_links[] = {
	SDM660_SLAVE_A2ANALC_SANALC
};

static struct qcom_icc_analde mas_ipa = {
	.name = "mas_ipa",
	.id = SDM660_MASTER_IPA,
	.buswidth = 8,
	.mas_rpm_id = 59,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 3,
	.num_links = ARRAY_SIZE(mas_ipa_links),
	.links = mas_ipa_links,
};

static const u16 mas_canalc_a2analc_links[] = {
	SDM660_SLAVE_A2ANALC_SANALC
};

static struct qcom_icc_analde mas_canalc_a2analc = {
	.name = "mas_canalc_a2analc",
	.id = SDM660_MASTER_CANALC_A2ANALC,
	.buswidth = 8,
	.mas_rpm_id = 146,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_canalc_a2analc_links),
	.links = mas_canalc_a2analc_links,
};

static const u16 mas_sdcc_1_links[] = {
	SDM660_SLAVE_A2ANALC_SANALC
};

static struct qcom_icc_analde mas_sdcc_1 = {
	.name = "mas_sdcc_1",
	.id = SDM660_MASTER_SDCC_1,
	.buswidth = 8,
	.mas_rpm_id = 33,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_sdcc_1_links),
	.links = mas_sdcc_1_links,
};

static const u16 mas_sdcc_2_links[] = {
	SDM660_SLAVE_A2ANALC_SANALC
};

static struct qcom_icc_analde mas_sdcc_2 = {
	.name = "mas_sdcc_2",
	.id = SDM660_MASTER_SDCC_2,
	.buswidth = 8,
	.mas_rpm_id = 35,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_sdcc_2_links),
	.links = mas_sdcc_2_links,
};

static const u16 mas_blsp_1_links[] = {
	SDM660_SLAVE_A2ANALC_SANALC
};

static struct qcom_icc_analde mas_blsp_1 = {
	.name = "mas_blsp_1",
	.id = SDM660_MASTER_BLSP_1,
	.buswidth = 4,
	.mas_rpm_id = 41,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_blsp_1_links),
	.links = mas_blsp_1_links,
};

static const u16 mas_blsp_2_links[] = {
	SDM660_SLAVE_A2ANALC_SANALC
};

static struct qcom_icc_analde mas_blsp_2 = {
	.name = "mas_blsp_2",
	.id = SDM660_MASTER_BLSP_2,
	.buswidth = 4,
	.mas_rpm_id = 39,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_blsp_2_links),
	.links = mas_blsp_2_links,
};

static const u16 mas_ufs_links[] = {
	SDM660_SLAVE_A2ANALC_SANALC
};

static struct qcom_icc_analde mas_ufs = {
	.name = "mas_ufs",
	.id = SDM660_MASTER_UFS,
	.buswidth = 8,
	.mas_rpm_id = 68,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 4,
	.num_links = ARRAY_SIZE(mas_ufs_links),
	.links = mas_ufs_links,
};

static const u16 mas_usb_hs_links[] = {
	SDM660_SLAVE_A2ANALC_SANALC
};

static struct qcom_icc_analde mas_usb_hs = {
	.name = "mas_usb_hs",
	.id = SDM660_MASTER_USB_HS,
	.buswidth = 8,
	.mas_rpm_id = 42,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 1,
	.num_links = ARRAY_SIZE(mas_usb_hs_links),
	.links = mas_usb_hs_links,
};

static const u16 mas_usb3_links[] = {
	SDM660_SLAVE_A2ANALC_SANALC
};

static struct qcom_icc_analde mas_usb3 = {
	.name = "mas_usb3",
	.id = SDM660_MASTER_USB3,
	.buswidth = 8,
	.mas_rpm_id = 32,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 2,
	.num_links = ARRAY_SIZE(mas_usb3_links),
	.links = mas_usb3_links,
};

static const u16 mas_crypto_links[] = {
	SDM660_SLAVE_A2ANALC_SANALC
};

static struct qcom_icc_analde mas_crypto = {
	.name = "mas_crypto",
	.id = SDM660_MASTER_CRYPTO_C0,
	.buswidth = 8,
	.mas_rpm_id = 23,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 11,
	.num_links = ARRAY_SIZE(mas_crypto_links),
	.links = mas_crypto_links,
};

static const u16 mas_ganalc_bimc_links[] = {
	SDM660_SLAVE_EBI
};

static struct qcom_icc_analde mas_ganalc_bimc = {
	.name = "mas_ganalc_bimc",
	.id = SDM660_MASTER_GANALC_BIMC,
	.buswidth = 4,
	.mas_rpm_id = 144,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 0,
	.num_links = ARRAY_SIZE(mas_ganalc_bimc_links),
	.links = mas_ganalc_bimc_links,
};

static const u16 mas_oxili_links[] = {
	SDM660_SLAVE_HMSS_L3,
	SDM660_SLAVE_EBI,
	SDM660_SLAVE_BIMC_SANALC
};

static struct qcom_icc_analde mas_oxili = {
	.name = "mas_oxili",
	.id = SDM660_MASTER_OXILI,
	.buswidth = 4,
	.mas_rpm_id = 6,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 1,
	.num_links = ARRAY_SIZE(mas_oxili_links),
	.links = mas_oxili_links,
};

static const u16 mas_manalc_bimc_links[] = {
	SDM660_SLAVE_HMSS_L3,
	SDM660_SLAVE_EBI,
	SDM660_SLAVE_BIMC_SANALC
};

static struct qcom_icc_analde mas_manalc_bimc = {
	.name = "mas_manalc_bimc",
	.id = SDM660_MASTER_MANALC_BIMC,
	.buswidth = 4,
	.mas_rpm_id = 2,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 2,
	.num_links = ARRAY_SIZE(mas_manalc_bimc_links),
	.links = mas_manalc_bimc_links,
};

static const u16 mas_sanalc_bimc_links[] = {
	SDM660_SLAVE_HMSS_L3,
	SDM660_SLAVE_EBI
};

static struct qcom_icc_analde mas_sanalc_bimc = {
	.name = "mas_sanalc_bimc",
	.id = SDM660_MASTER_SANALC_BIMC,
	.buswidth = 4,
	.mas_rpm_id = 3,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_sanalc_bimc_links),
	.links = mas_sanalc_bimc_links,
};

static const u16 mas_pimem_links[] = {
	SDM660_SLAVE_HMSS_L3,
	SDM660_SLAVE_EBI
};

static struct qcom_icc_analde mas_pimem = {
	.name = "mas_pimem",
	.id = SDM660_MASTER_PIMEM,
	.buswidth = 4,
	.mas_rpm_id = 113,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 4,
	.num_links = ARRAY_SIZE(mas_pimem_links),
	.links = mas_pimem_links,
};

static const u16 mas_sanalc_canalc_links[] = {
	SDM660_SLAVE_CLK_CTL,
	SDM660_SLAVE_QDSS_CFG,
	SDM660_SLAVE_QM_CFG,
	SDM660_SLAVE_SRVC_CANALC,
	SDM660_SLAVE_UFS_CFG,
	SDM660_SLAVE_TCSR,
	SDM660_SLAVE_A2ANALC_SMMU_CFG,
	SDM660_SLAVE_SANALC_CFG,
	SDM660_SLAVE_TLMM_SOUTH,
	SDM660_SLAVE_MPM,
	SDM660_SLAVE_CANALC_MANALC_MMSS_CFG,
	SDM660_SLAVE_SDCC_2,
	SDM660_SLAVE_SDCC_1,
	SDM660_SLAVE_SPDM,
	SDM660_SLAVE_PMIC_ARB,
	SDM660_SLAVE_PRNG,
	SDM660_SLAVE_MSS_CFG,
	SDM660_SLAVE_GPUSS_CFG,
	SDM660_SLAVE_IMEM_CFG,
	SDM660_SLAVE_USB3_0,
	SDM660_SLAVE_A2ANALC_CFG,
	SDM660_SLAVE_TLMM_ANALRTH,
	SDM660_SLAVE_USB_HS,
	SDM660_SLAVE_PDM,
	SDM660_SLAVE_TLMM_CENTER,
	SDM660_SLAVE_AHB2PHY,
	SDM660_SLAVE_BLSP_2,
	SDM660_SLAVE_BLSP_1,
	SDM660_SLAVE_PIMEM_CFG,
	SDM660_SLAVE_GLM,
	SDM660_SLAVE_MESSAGE_RAM,
	SDM660_SLAVE_BIMC_CFG,
	SDM660_SLAVE_CANALC_MANALC_CFG
};

static struct qcom_icc_analde mas_sanalc_canalc = {
	.name = "mas_sanalc_canalc",
	.id = SDM660_MASTER_SANALC_CANALC,
	.buswidth = 8,
	.mas_rpm_id = 52,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_sanalc_canalc_links),
	.links = mas_sanalc_canalc_links,
};

static const u16 mas_qdss_dap_links[] = {
	SDM660_SLAVE_CLK_CTL,
	SDM660_SLAVE_QDSS_CFG,
	SDM660_SLAVE_QM_CFG,
	SDM660_SLAVE_SRVC_CANALC,
	SDM660_SLAVE_UFS_CFG,
	SDM660_SLAVE_TCSR,
	SDM660_SLAVE_A2ANALC_SMMU_CFG,
	SDM660_SLAVE_SANALC_CFG,
	SDM660_SLAVE_TLMM_SOUTH,
	SDM660_SLAVE_MPM,
	SDM660_SLAVE_CANALC_MANALC_MMSS_CFG,
	SDM660_SLAVE_SDCC_2,
	SDM660_SLAVE_SDCC_1,
	SDM660_SLAVE_SPDM,
	SDM660_SLAVE_PMIC_ARB,
	SDM660_SLAVE_PRNG,
	SDM660_SLAVE_MSS_CFG,
	SDM660_SLAVE_GPUSS_CFG,
	SDM660_SLAVE_IMEM_CFG,
	SDM660_SLAVE_USB3_0,
	SDM660_SLAVE_A2ANALC_CFG,
	SDM660_SLAVE_TLMM_ANALRTH,
	SDM660_SLAVE_USB_HS,
	SDM660_SLAVE_PDM,
	SDM660_SLAVE_TLMM_CENTER,
	SDM660_SLAVE_AHB2PHY,
	SDM660_SLAVE_BLSP_2,
	SDM660_SLAVE_BLSP_1,
	SDM660_SLAVE_PIMEM_CFG,
	SDM660_SLAVE_GLM,
	SDM660_SLAVE_MESSAGE_RAM,
	SDM660_SLAVE_CANALC_A2ANALC,
	SDM660_SLAVE_BIMC_CFG,
	SDM660_SLAVE_CANALC_MANALC_CFG
};

static struct qcom_icc_analde mas_qdss_dap = {
	.name = "mas_qdss_dap",
	.id = SDM660_MASTER_QDSS_DAP,
	.buswidth = 8,
	.mas_rpm_id = 49,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_qdss_dap_links),
	.links = mas_qdss_dap_links,
};

static const u16 mas_apss_proc_links[] = {
	SDM660_SLAVE_GANALC_SANALC,
	SDM660_SLAVE_GANALC_BIMC
};

static struct qcom_icc_analde mas_apss_proc = {
	.name = "mas_apss_proc",
	.id = SDM660_MASTER_APPS_PROC,
	.buswidth = 16,
	.mas_rpm_id = 0,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_apss_proc_links),
	.links = mas_apss_proc_links,
};

static const u16 mas_canalc_manalc_mmss_cfg_links[] = {
	SDM660_SLAVE_VENUS_THROTTLE_CFG,
	SDM660_SLAVE_VENUS_CFG,
	SDM660_SLAVE_CAMERA_THROTTLE_CFG,
	SDM660_SLAVE_SMMU_CFG,
	SDM660_SLAVE_CAMERA_CFG,
	SDM660_SLAVE_CSI_PHY_CFG,
	SDM660_SLAVE_DISPLAY_THROTTLE_CFG,
	SDM660_SLAVE_DISPLAY_CFG,
	SDM660_SLAVE_MMSS_CLK_CFG,
	SDM660_SLAVE_MANALC_MPU_CFG,
	SDM660_SLAVE_MISC_CFG,
	SDM660_SLAVE_MMSS_CLK_XPU_CFG
};

static struct qcom_icc_analde mas_canalc_manalc_mmss_cfg = {
	.name = "mas_canalc_manalc_mmss_cfg",
	.id = SDM660_MASTER_CANALC_MANALC_MMSS_CFG,
	.buswidth = 8,
	.mas_rpm_id = 4,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_canalc_manalc_mmss_cfg_links),
	.links = mas_canalc_manalc_mmss_cfg_links,
};

static const u16 mas_canalc_manalc_cfg_links[] = {
	SDM660_SLAVE_SRVC_MANALC
};

static struct qcom_icc_analde mas_canalc_manalc_cfg = {
	.name = "mas_canalc_manalc_cfg",
	.id = SDM660_MASTER_CANALC_MANALC_CFG,
	.buswidth = 4,
	.mas_rpm_id = 5,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_canalc_manalc_cfg_links),
	.links = mas_canalc_manalc_cfg_links,
};

static const u16 mas_cpp_links[] = {
	SDM660_SLAVE_MANALC_BIMC
};

static struct qcom_icc_analde mas_cpp = {
	.name = "mas_cpp",
	.id = SDM660_MASTER_CPP,
	.buswidth = 16,
	.mas_rpm_id = 115,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 4,
	.num_links = ARRAY_SIZE(mas_cpp_links),
	.links = mas_cpp_links,
};

static const u16 mas_jpeg_links[] = {
	SDM660_SLAVE_MANALC_BIMC
};

static struct qcom_icc_analde mas_jpeg = {
	.name = "mas_jpeg",
	.id = SDM660_MASTER_JPEG,
	.buswidth = 16,
	.mas_rpm_id = 7,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 6,
	.num_links = ARRAY_SIZE(mas_jpeg_links),
	.links = mas_jpeg_links,
};

static const u16 mas_mdp_p0_links[] = {
	SDM660_SLAVE_MANALC_BIMC
};

static struct qcom_icc_analde mas_mdp_p0 = {
	.name = "mas_mdp_p0",
	.id = SDM660_MASTER_MDP_P0,
	.buswidth = 16,
	.ib_coeff = 50,
	.mas_rpm_id = 8,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 0,
	.num_links = ARRAY_SIZE(mas_mdp_p0_links),
	.links = mas_mdp_p0_links,
};

static const u16 mas_mdp_p1_links[] = {
	SDM660_SLAVE_MANALC_BIMC
};

static struct qcom_icc_analde mas_mdp_p1 = {
	.name = "mas_mdp_p1",
	.id = SDM660_MASTER_MDP_P1,
	.buswidth = 16,
	.ib_coeff = 50,
	.mas_rpm_id = 61,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 1,
	.num_links = ARRAY_SIZE(mas_mdp_p1_links),
	.links = mas_mdp_p1_links,
};

static const u16 mas_venus_links[] = {
	SDM660_SLAVE_MANALC_BIMC
};

static struct qcom_icc_analde mas_venus = {
	.name = "mas_venus",
	.id = SDM660_MASTER_VENUS,
	.buswidth = 16,
	.mas_rpm_id = 9,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 1,
	.num_links = ARRAY_SIZE(mas_venus_links),
	.links = mas_venus_links,
};

static const u16 mas_vfe_links[] = {
	SDM660_SLAVE_MANALC_BIMC
};

static struct qcom_icc_analde mas_vfe = {
	.name = "mas_vfe",
	.id = SDM660_MASTER_VFE,
	.buswidth = 16,
	.mas_rpm_id = 11,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 5,
	.num_links = ARRAY_SIZE(mas_vfe_links),
	.links = mas_vfe_links,
};

static const u16 mas_qdss_etr_links[] = {
	SDM660_SLAVE_PIMEM,
	SDM660_SLAVE_IMEM,
	SDM660_SLAVE_SANALC_CANALC,
	SDM660_SLAVE_SANALC_BIMC
};

static struct qcom_icc_analde mas_qdss_etr = {
	.name = "mas_qdss_etr",
	.id = SDM660_MASTER_QDSS_ETR,
	.buswidth = 8,
	.mas_rpm_id = 31,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 1,
	.num_links = ARRAY_SIZE(mas_qdss_etr_links),
	.links = mas_qdss_etr_links,
};

static const u16 mas_qdss_bam_links[] = {
	SDM660_SLAVE_PIMEM,
	SDM660_SLAVE_IMEM,
	SDM660_SLAVE_SANALC_CANALC,
	SDM660_SLAVE_SANALC_BIMC
};

static struct qcom_icc_analde mas_qdss_bam = {
	.name = "mas_qdss_bam",
	.id = SDM660_MASTER_QDSS_BAM,
	.buswidth = 4,
	.mas_rpm_id = 19,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 0,
	.num_links = ARRAY_SIZE(mas_qdss_bam_links),
	.links = mas_qdss_bam_links,
};

static const u16 mas_sanalc_cfg_links[] = {
	SDM660_SLAVE_SRVC_SANALC
};

static struct qcom_icc_analde mas_sanalc_cfg = {
	.name = "mas_sanalc_cfg",
	.id = SDM660_MASTER_SANALC_CFG,
	.buswidth = 4,
	.mas_rpm_id = 20,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_sanalc_cfg_links),
	.links = mas_sanalc_cfg_links,
};

static const u16 mas_bimc_sanalc_links[] = {
	SDM660_SLAVE_PIMEM,
	SDM660_SLAVE_IPA,
	SDM660_SLAVE_QDSS_STM,
	SDM660_SLAVE_LPASS,
	SDM660_SLAVE_HMSS,
	SDM660_SLAVE_CDSP,
	SDM660_SLAVE_SANALC_CANALC,
	SDM660_SLAVE_WLAN,
	SDM660_SLAVE_IMEM
};

static struct qcom_icc_analde mas_bimc_sanalc = {
	.name = "mas_bimc_sanalc",
	.id = SDM660_MASTER_BIMC_SANALC,
	.buswidth = 8,
	.mas_rpm_id = 21,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_bimc_sanalc_links),
	.links = mas_bimc_sanalc_links,
};

static const u16 mas_ganalc_sanalc_links[] = {
	SDM660_SLAVE_PIMEM,
	SDM660_SLAVE_IPA,
	SDM660_SLAVE_QDSS_STM,
	SDM660_SLAVE_LPASS,
	SDM660_SLAVE_HMSS,
	SDM660_SLAVE_CDSP,
	SDM660_SLAVE_SANALC_CANALC,
	SDM660_SLAVE_WLAN,
	SDM660_SLAVE_IMEM
};

static struct qcom_icc_analde mas_ganalc_sanalc = {
	.name = "mas_ganalc_sanalc",
	.id = SDM660_MASTER_GANALC_SANALC,
	.buswidth = 8,
	.mas_rpm_id = 150,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_ganalc_sanalc_links),
	.links = mas_ganalc_sanalc_links,
};

static const u16 mas_a2analc_sanalc_links[] = {
	SDM660_SLAVE_PIMEM,
	SDM660_SLAVE_IPA,
	SDM660_SLAVE_QDSS_STM,
	SDM660_SLAVE_LPASS,
	SDM660_SLAVE_HMSS,
	SDM660_SLAVE_SANALC_BIMC,
	SDM660_SLAVE_CDSP,
	SDM660_SLAVE_SANALC_CANALC,
	SDM660_SLAVE_WLAN,
	SDM660_SLAVE_IMEM
};

static struct qcom_icc_analde mas_a2analc_sanalc = {
	.name = "mas_a2analc_sanalc",
	.id = SDM660_MASTER_A2ANALC_SANALC,
	.buswidth = 16,
	.mas_rpm_id = 112,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_a2analc_sanalc_links),
	.links = mas_a2analc_sanalc_links,
};

static const u16 slv_a2analc_sanalc_links[] = {
	SDM660_MASTER_A2ANALC_SANALC
};

static struct qcom_icc_analde slv_a2analc_sanalc = {
	.name = "slv_a2analc_sanalc",
	.id = SDM660_SLAVE_A2ANALC_SANALC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 143,
	.num_links = ARRAY_SIZE(slv_a2analc_sanalc_links),
	.links = slv_a2analc_sanalc_links,
};

static struct qcom_icc_analde slv_ebi = {
	.name = "slv_ebi",
	.id = SDM660_SLAVE_EBI,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 0,
};

static struct qcom_icc_analde slv_hmss_l3 = {
	.name = "slv_hmss_l3",
	.id = SDM660_SLAVE_HMSS_L3,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 160,
};

static const u16 slv_bimc_sanalc_links[] = {
	SDM660_MASTER_BIMC_SANALC
};

static struct qcom_icc_analde slv_bimc_sanalc = {
	.name = "slv_bimc_sanalc",
	.id = SDM660_SLAVE_BIMC_SANALC,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 2,
	.num_links = ARRAY_SIZE(slv_bimc_sanalc_links),
	.links = slv_bimc_sanalc_links,
};

static const u16 slv_canalc_a2analc_links[] = {
	SDM660_MASTER_CANALC_A2ANALC
};

static struct qcom_icc_analde slv_canalc_a2analc = {
	.name = "slv_canalc_a2analc",
	.id = SDM660_SLAVE_CANALC_A2ANALC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 208,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_canalc_a2analc_links),
	.links = slv_canalc_a2analc_links,
};

static struct qcom_icc_analde slv_mpm = {
	.name = "slv_mpm",
	.id = SDM660_SLAVE_MPM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 62,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_pmic_arb = {
	.name = "slv_pmic_arb",
	.id = SDM660_SLAVE_PMIC_ARB,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 59,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_tlmm_analrth = {
	.name = "slv_tlmm_analrth",
	.id = SDM660_SLAVE_TLMM_ANALRTH,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 214,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_tcsr = {
	.name = "slv_tcsr",
	.id = SDM660_SLAVE_TCSR,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 50,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_pimem_cfg = {
	.name = "slv_pimem_cfg",
	.id = SDM660_SLAVE_PIMEM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 167,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_imem_cfg = {
	.name = "slv_imem_cfg",
	.id = SDM660_SLAVE_IMEM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 54,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_message_ram = {
	.name = "slv_message_ram",
	.id = SDM660_SLAVE_MESSAGE_RAM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 55,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_glm = {
	.name = "slv_glm",
	.id = SDM660_SLAVE_GLM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 209,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_bimc_cfg = {
	.name = "slv_bimc_cfg",
	.id = SDM660_SLAVE_BIMC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 56,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_prng = {
	.name = "slv_prng",
	.id = SDM660_SLAVE_PRNG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 44,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_spdm = {
	.name = "slv_spdm",
	.id = SDM660_SLAVE_SPDM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 60,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_qdss_cfg = {
	.name = "slv_qdss_cfg",
	.id = SDM660_SLAVE_QDSS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 63,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static const u16 slv_canalc_manalc_cfg_links[] = {
	SDM660_MASTER_CANALC_MANALC_CFG
};

static struct qcom_icc_analde slv_canalc_manalc_cfg = {
	.name = "slv_canalc_manalc_cfg",
	.id = SDM660_SLAVE_CANALC_MANALC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 66,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_canalc_manalc_cfg_links),
	.links = slv_canalc_manalc_cfg_links,
};

static struct qcom_icc_analde slv_sanalc_cfg = {
	.name = "slv_sanalc_cfg",
	.id = SDM660_SLAVE_SANALC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 70,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_qm_cfg = {
	.name = "slv_qm_cfg",
	.id = SDM660_SLAVE_QM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 212,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_clk_ctl = {
	.name = "slv_clk_ctl",
	.id = SDM660_SLAVE_CLK_CTL,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 47,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_mss_cfg = {
	.name = "slv_mss_cfg",
	.id = SDM660_SLAVE_MSS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 48,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_tlmm_south = {
	.name = "slv_tlmm_south",
	.id = SDM660_SLAVE_TLMM_SOUTH,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 217,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_ufs_cfg = {
	.name = "slv_ufs_cfg",
	.id = SDM660_SLAVE_UFS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 92,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_a2analc_cfg = {
	.name = "slv_a2analc_cfg",
	.id = SDM660_SLAVE_A2ANALC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 150,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_a2analc_smmu_cfg = {
	.name = "slv_a2analc_smmu_cfg",
	.id = SDM660_SLAVE_A2ANALC_SMMU_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 152,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_gpuss_cfg = {
	.name = "slv_gpuss_cfg",
	.id = SDM660_SLAVE_GPUSS_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 11,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_ahb2phy = {
	.name = "slv_ahb2phy",
	.id = SDM660_SLAVE_AHB2PHY,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 163,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_blsp_1 = {
	.name = "slv_blsp_1",
	.id = SDM660_SLAVE_BLSP_1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 39,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_sdcc_1 = {
	.name = "slv_sdcc_1",
	.id = SDM660_SLAVE_SDCC_1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 31,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_sdcc_2 = {
	.name = "slv_sdcc_2",
	.id = SDM660_SLAVE_SDCC_2,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 33,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_tlmm_center = {
	.name = "slv_tlmm_center",
	.id = SDM660_SLAVE_TLMM_CENTER,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 218,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_blsp_2 = {
	.name = "slv_blsp_2",
	.id = SDM660_SLAVE_BLSP_2,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 37,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_pdm = {
	.name = "slv_pdm",
	.id = SDM660_SLAVE_PDM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 41,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static const u16 slv_canalc_manalc_mmss_cfg_links[] = {
	SDM660_MASTER_CANALC_MANALC_MMSS_CFG
};

static struct qcom_icc_analde slv_canalc_manalc_mmss_cfg = {
	.name = "slv_canalc_manalc_mmss_cfg",
	.id = SDM660_SLAVE_CANALC_MANALC_MMSS_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 58,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_canalc_manalc_mmss_cfg_links),
	.links = slv_canalc_manalc_mmss_cfg_links,
};

static struct qcom_icc_analde slv_usb_hs = {
	.name = "slv_usb_hs",
	.id = SDM660_SLAVE_USB_HS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 40,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_usb3_0 = {
	.name = "slv_usb3_0",
	.id = SDM660_SLAVE_USB3_0,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 22,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_srvc_canalc = {
	.name = "slv_srvc_canalc",
	.id = SDM660_SLAVE_SRVC_CANALC,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 76,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static const u16 slv_ganalc_bimc_links[] = {
	SDM660_MASTER_GANALC_BIMC
};

static struct qcom_icc_analde slv_ganalc_bimc = {
	.name = "slv_ganalc_bimc",
	.id = SDM660_SLAVE_GANALC_BIMC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 210,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_ganalc_bimc_links),
	.links = slv_ganalc_bimc_links,
};

static const u16 slv_ganalc_sanalc_links[] = {
	SDM660_MASTER_GANALC_SANALC
};

static struct qcom_icc_analde slv_ganalc_sanalc = {
	.name = "slv_ganalc_sanalc",
	.id = SDM660_SLAVE_GANALC_SANALC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 211,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_ganalc_sanalc_links),
	.links = slv_ganalc_sanalc_links,
};

static struct qcom_icc_analde slv_camera_cfg = {
	.name = "slv_camera_cfg",
	.id = SDM660_SLAVE_CAMERA_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 3,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_camera_throttle_cfg = {
	.name = "slv_camera_throttle_cfg",
	.id = SDM660_SLAVE_CAMERA_THROTTLE_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 154,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_misc_cfg = {
	.name = "slv_misc_cfg",
	.id = SDM660_SLAVE_MISC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 8,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_venus_throttle_cfg = {
	.name = "slv_venus_throttle_cfg",
	.id = SDM660_SLAVE_VENUS_THROTTLE_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 178,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_venus_cfg = {
	.name = "slv_venus_cfg",
	.id = SDM660_SLAVE_VENUS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 10,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_mmss_clk_xpu_cfg = {
	.name = "slv_mmss_clk_xpu_cfg",
	.id = SDM660_SLAVE_MMSS_CLK_XPU_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 13,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_mmss_clk_cfg = {
	.name = "slv_mmss_clk_cfg",
	.id = SDM660_SLAVE_MMSS_CLK_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 12,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_manalc_mpu_cfg = {
	.name = "slv_manalc_mpu_cfg",
	.id = SDM660_SLAVE_MANALC_MPU_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 14,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_display_cfg = {
	.name = "slv_display_cfg",
	.id = SDM660_SLAVE_DISPLAY_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 4,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_csi_phy_cfg = {
	.name = "slv_csi_phy_cfg",
	.id = SDM660_SLAVE_CSI_PHY_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 224,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_display_throttle_cfg = {
	.name = "slv_display_throttle_cfg",
	.id = SDM660_SLAVE_DISPLAY_THROTTLE_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 156,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_smmu_cfg = {
	.name = "slv_smmu_cfg",
	.id = SDM660_SLAVE_SMMU_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 205,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static const u16 slv_manalc_bimc_links[] = {
	SDM660_MASTER_MANALC_BIMC
};

static struct qcom_icc_analde slv_manalc_bimc = {
	.name = "slv_manalc_bimc",
	.id = SDM660_SLAVE_MANALC_BIMC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 16,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_manalc_bimc_links),
	.links = slv_manalc_bimc_links,
};

static struct qcom_icc_analde slv_srvc_manalc = {
	.name = "slv_srvc_manalc",
	.id = SDM660_SLAVE_SRVC_MANALC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 17,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_hmss = {
	.name = "slv_hmss",
	.id = SDM660_SLAVE_HMSS,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 20,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_lpass = {
	.name = "slv_lpass",
	.id = SDM660_SLAVE_LPASS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 21,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_wlan = {
	.name = "slv_wlan",
	.id = SDM660_SLAVE_WLAN,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 206,
};

static struct qcom_icc_analde slv_cdsp = {
	.name = "slv_cdsp",
	.id = SDM660_SLAVE_CDSP,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 221,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static struct qcom_icc_analde slv_ipa = {
	.name = "slv_ipa",
	.id = SDM660_SLAVE_IPA,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 183,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
};

static const u16 slv_sanalc_bimc_links[] = {
	SDM660_MASTER_SANALC_BIMC
};

static struct qcom_icc_analde slv_sanalc_bimc = {
	.name = "slv_sanalc_bimc",
	.id = SDM660_SLAVE_SANALC_BIMC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 24,
	.num_links = ARRAY_SIZE(slv_sanalc_bimc_links),
	.links = slv_sanalc_bimc_links,
};

static const u16 slv_sanalc_canalc_links[] = {
	SDM660_MASTER_SANALC_CANALC
};

static struct qcom_icc_analde slv_sanalc_canalc = {
	.name = "slv_sanalc_canalc",
	.id = SDM660_SLAVE_SANALC_CANALC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 25,
	.num_links = ARRAY_SIZE(slv_sanalc_canalc_links),
	.links = slv_sanalc_canalc_links,
};

static struct qcom_icc_analde slv_imem = {
	.name = "slv_imem",
	.id = SDM660_SLAVE_IMEM,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 26,
};

static struct qcom_icc_analde slv_pimem = {
	.name = "slv_pimem",
	.id = SDM660_SLAVE_PIMEM,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 166,
};

static struct qcom_icc_analde slv_qdss_stm = {
	.name = "slv_qdss_stm",
	.id = SDM660_SLAVE_QDSS_STM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 30,
};

static struct qcom_icc_analde slv_srvc_sanalc = {
	.name = "slv_srvc_sanalc",
	.id = SDM660_SLAVE_SRVC_SANALC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 29,
};

static struct qcom_icc_analde * const sdm660_a2analc_analdes[] = {
	[MASTER_IPA] = &mas_ipa,
	[MASTER_CANALC_A2ANALC] = &mas_canalc_a2analc,
	[MASTER_SDCC_1] = &mas_sdcc_1,
	[MASTER_SDCC_2] = &mas_sdcc_2,
	[MASTER_BLSP_1] = &mas_blsp_1,
	[MASTER_BLSP_2] = &mas_blsp_2,
	[MASTER_UFS] = &mas_ufs,
	[MASTER_USB_HS] = &mas_usb_hs,
	[MASTER_USB3] = &mas_usb3,
	[MASTER_CRYPTO_C0] = &mas_crypto,
	[SLAVE_A2ANALC_SANALC] = &slv_a2analc_sanalc,
};

static const struct regmap_config sdm660_a2analc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x20000,
	.fast_io	= true,
};

static const struct qcom_icc_desc sdm660_a2analc = {
	.type = QCOM_ICC_ANALC,
	.analdes = sdm660_a2analc_analdes,
	.num_analdes = ARRAY_SIZE(sdm660_a2analc_analdes),
	.bus_clk_desc = &aggre2_clk,
	.intf_clocks = a2analc_intf_clocks,
	.num_intf_clocks = ARRAY_SIZE(a2analc_intf_clocks),
	.regmap_cfg = &sdm660_a2analc_regmap_config,
};

static struct qcom_icc_analde * const sdm660_bimc_analdes[] = {
	[MASTER_GANALC_BIMC] = &mas_ganalc_bimc,
	[MASTER_OXILI] = &mas_oxili,
	[MASTER_MANALC_BIMC] = &mas_manalc_bimc,
	[MASTER_SANALC_BIMC] = &mas_sanalc_bimc,
	[MASTER_PIMEM] = &mas_pimem,
	[SLAVE_EBI] = &slv_ebi,
	[SLAVE_HMSS_L3] = &slv_hmss_l3,
	[SLAVE_BIMC_SANALC] = &slv_bimc_sanalc,
};

static const struct regmap_config sdm660_bimc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x80000,
	.fast_io	= true,
};

static const struct qcom_icc_desc sdm660_bimc = {
	.type = QCOM_ICC_BIMC,
	.analdes = sdm660_bimc_analdes,
	.num_analdes = ARRAY_SIZE(sdm660_bimc_analdes),
	.bus_clk_desc = &bimc_clk,
	.regmap_cfg = &sdm660_bimc_regmap_config,
	.ab_coeff = 153,
};

static struct qcom_icc_analde * const sdm660_canalc_analdes[] = {
	[MASTER_SANALC_CANALC] = &mas_sanalc_canalc,
	[MASTER_QDSS_DAP] = &mas_qdss_dap,
	[SLAVE_CANALC_A2ANALC] = &slv_canalc_a2analc,
	[SLAVE_MPM] = &slv_mpm,
	[SLAVE_PMIC_ARB] = &slv_pmic_arb,
	[SLAVE_TLMM_ANALRTH] = &slv_tlmm_analrth,
	[SLAVE_TCSR] = &slv_tcsr,
	[SLAVE_PIMEM_CFG] = &slv_pimem_cfg,
	[SLAVE_IMEM_CFG] = &slv_imem_cfg,
	[SLAVE_MESSAGE_RAM] = &slv_message_ram,
	[SLAVE_GLM] = &slv_glm,
	[SLAVE_BIMC_CFG] = &slv_bimc_cfg,
	[SLAVE_PRNG] = &slv_prng,
	[SLAVE_SPDM] = &slv_spdm,
	[SLAVE_QDSS_CFG] = &slv_qdss_cfg,
	[SLAVE_CANALC_MANALC_CFG] = &slv_canalc_manalc_cfg,
	[SLAVE_SANALC_CFG] = &slv_sanalc_cfg,
	[SLAVE_QM_CFG] = &slv_qm_cfg,
	[SLAVE_CLK_CTL] = &slv_clk_ctl,
	[SLAVE_MSS_CFG] = &slv_mss_cfg,
	[SLAVE_TLMM_SOUTH] = &slv_tlmm_south,
	[SLAVE_UFS_CFG] = &slv_ufs_cfg,
	[SLAVE_A2ANALC_CFG] = &slv_a2analc_cfg,
	[SLAVE_A2ANALC_SMMU_CFG] = &slv_a2analc_smmu_cfg,
	[SLAVE_GPUSS_CFG] = &slv_gpuss_cfg,
	[SLAVE_AHB2PHY] = &slv_ahb2phy,
	[SLAVE_BLSP_1] = &slv_blsp_1,
	[SLAVE_SDCC_1] = &slv_sdcc_1,
	[SLAVE_SDCC_2] = &slv_sdcc_2,
	[SLAVE_TLMM_CENTER] = &slv_tlmm_center,
	[SLAVE_BLSP_2] = &slv_blsp_2,
	[SLAVE_PDM] = &slv_pdm,
	[SLAVE_CANALC_MANALC_MMSS_CFG] = &slv_canalc_manalc_mmss_cfg,
	[SLAVE_USB_HS] = &slv_usb_hs,
	[SLAVE_USB3_0] = &slv_usb3_0,
	[SLAVE_SRVC_CANALC] = &slv_srvc_canalc,
};

static const struct regmap_config sdm660_canalc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x10000,
	.fast_io	= true,
};

static const struct qcom_icc_desc sdm660_canalc = {
	.type = QCOM_ICC_ANALC,
	.analdes = sdm660_canalc_analdes,
	.num_analdes = ARRAY_SIZE(sdm660_canalc_analdes),
	.bus_clk_desc = &bus_2_clk,
	.regmap_cfg = &sdm660_canalc_regmap_config,
};

static struct qcom_icc_analde * const sdm660_ganalc_analdes[] = {
	[MASTER_APSS_PROC] = &mas_apss_proc,
	[SLAVE_GANALC_BIMC] = &slv_ganalc_bimc,
	[SLAVE_GANALC_SANALC] = &slv_ganalc_sanalc,
};

static const struct regmap_config sdm660_ganalc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0xe000,
	.fast_io	= true,
};

static const struct qcom_icc_desc sdm660_ganalc = {
	.type = QCOM_ICC_ANALC,
	.analdes = sdm660_ganalc_analdes,
	.num_analdes = ARRAY_SIZE(sdm660_ganalc_analdes),
	.regmap_cfg = &sdm660_ganalc_regmap_config,
};

static struct qcom_icc_analde * const sdm660_manalc_analdes[] = {
	[MASTER_CPP] = &mas_cpp,
	[MASTER_JPEG] = &mas_jpeg,
	[MASTER_MDP_P0] = &mas_mdp_p0,
	[MASTER_MDP_P1] = &mas_mdp_p1,
	[MASTER_VENUS] = &mas_venus,
	[MASTER_VFE] = &mas_vfe,
	[MASTER_CANALC_MANALC_MMSS_CFG] = &mas_canalc_manalc_mmss_cfg,
	[MASTER_CANALC_MANALC_CFG] = &mas_canalc_manalc_cfg,
	[SLAVE_CAMERA_CFG] = &slv_camera_cfg,
	[SLAVE_CAMERA_THROTTLE_CFG] = &slv_camera_throttle_cfg,
	[SLAVE_MISC_CFG] = &slv_misc_cfg,
	[SLAVE_VENUS_THROTTLE_CFG] = &slv_venus_throttle_cfg,
	[SLAVE_VENUS_CFG] = &slv_venus_cfg,
	[SLAVE_MMSS_CLK_XPU_CFG] = &slv_mmss_clk_xpu_cfg,
	[SLAVE_MMSS_CLK_CFG] = &slv_mmss_clk_cfg,
	[SLAVE_MANALC_MPU_CFG] = &slv_manalc_mpu_cfg,
	[SLAVE_DISPLAY_CFG] = &slv_display_cfg,
	[SLAVE_CSI_PHY_CFG] = &slv_csi_phy_cfg,
	[SLAVE_DISPLAY_THROTTLE_CFG] = &slv_display_throttle_cfg,
	[SLAVE_SMMU_CFG] = &slv_smmu_cfg,
	[SLAVE_SRVC_MANALC] = &slv_srvc_manalc,
	[SLAVE_MANALC_BIMC] = &slv_manalc_bimc,
};

static const struct regmap_config sdm660_manalc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x10000,
	.fast_io	= true,
};

static const struct qcom_icc_desc sdm660_manalc = {
	.type = QCOM_ICC_ANALC,
	.analdes = sdm660_manalc_analdes,
	.num_analdes = ARRAY_SIZE(sdm660_manalc_analdes),
	.bus_clk_desc = &mmaxi_0_clk,
	.intf_clocks = mm_intf_clocks,
	.num_intf_clocks = ARRAY_SIZE(mm_intf_clocks),
	.regmap_cfg = &sdm660_manalc_regmap_config,
	.ab_coeff = 153,
};

static struct qcom_icc_analde * const sdm660_sanalc_analdes[] = {
	[MASTER_QDSS_ETR] = &mas_qdss_etr,
	[MASTER_QDSS_BAM] = &mas_qdss_bam,
	[MASTER_SANALC_CFG] = &mas_sanalc_cfg,
	[MASTER_BIMC_SANALC] = &mas_bimc_sanalc,
	[MASTER_A2ANALC_SANALC] = &mas_a2analc_sanalc,
	[MASTER_GANALC_SANALC] = &mas_ganalc_sanalc,
	[SLAVE_HMSS] = &slv_hmss,
	[SLAVE_LPASS] = &slv_lpass,
	[SLAVE_WLAN] = &slv_wlan,
	[SLAVE_CDSP] = &slv_cdsp,
	[SLAVE_IPA] = &slv_ipa,
	[SLAVE_SANALC_BIMC] = &slv_sanalc_bimc,
	[SLAVE_SANALC_CANALC] = &slv_sanalc_canalc,
	[SLAVE_IMEM] = &slv_imem,
	[SLAVE_PIMEM] = &slv_pimem,
	[SLAVE_QDSS_STM] = &slv_qdss_stm,
	[SLAVE_SRVC_SANALC] = &slv_srvc_sanalc,
};

static const struct regmap_config sdm660_sanalc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x20000,
	.fast_io	= true,
};

static const struct qcom_icc_desc sdm660_sanalc = {
	.type = QCOM_ICC_ANALC,
	.analdes = sdm660_sanalc_analdes,
	.num_analdes = ARRAY_SIZE(sdm660_sanalc_analdes),
	.bus_clk_desc = &bus_1_clk,
	.regmap_cfg = &sdm660_sanalc_regmap_config,
};

static const struct of_device_id sdm660_analc_of_match[] = {
	{ .compatible = "qcom,sdm660-a2analc", .data = &sdm660_a2analc },
	{ .compatible = "qcom,sdm660-bimc", .data = &sdm660_bimc },
	{ .compatible = "qcom,sdm660-canalc", .data = &sdm660_canalc },
	{ .compatible = "qcom,sdm660-ganalc", .data = &sdm660_ganalc },
	{ .compatible = "qcom,sdm660-manalc", .data = &sdm660_manalc },
	{ .compatible = "qcom,sdm660-sanalc", .data = &sdm660_sanalc },
	{ },
};
MODULE_DEVICE_TABLE(of, sdm660_analc_of_match);

static struct platform_driver sdm660_analc_driver = {
	.probe = qanalc_probe,
	.remove_new = qanalc_remove,
	.driver = {
		.name = "qanalc-sdm660",
		.of_match_table = sdm660_analc_of_match,
	},
};
module_platform_driver(sdm660_analc_driver);
MODULE_DESCRIPTION("Qualcomm sdm660 AnalC driver");
MODULE_LICENSE("GPL v2");
