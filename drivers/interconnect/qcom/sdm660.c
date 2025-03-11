// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm SDM630/SDM636/SDM660 Network-on-Chip (NoC) QoS driver
 * Copyright (C) 2020, AngeloGioacchino Del Regno <kholk11@gmail.com>
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
	SDM660_MASTER_CNOC_A2NOC,
	SDM660_MASTER_SDCC_1,
	SDM660_MASTER_SDCC_2,
	SDM660_MASTER_BLSP_1,
	SDM660_MASTER_BLSP_2,
	SDM660_MASTER_UFS,
	SDM660_MASTER_USB_HS,
	SDM660_MASTER_USB3,
	SDM660_MASTER_CRYPTO_C0,
	SDM660_MASTER_GNOC_BIMC,
	SDM660_MASTER_OXILI,
	SDM660_MASTER_MNOC_BIMC,
	SDM660_MASTER_SNOC_BIMC,
	SDM660_MASTER_PIMEM,
	SDM660_MASTER_SNOC_CNOC,
	SDM660_MASTER_QDSS_DAP,
	SDM660_MASTER_APPS_PROC,
	SDM660_MASTER_CNOC_MNOC_MMSS_CFG,
	SDM660_MASTER_CNOC_MNOC_CFG,
	SDM660_MASTER_CPP,
	SDM660_MASTER_JPEG,
	SDM660_MASTER_MDP_P0,
	SDM660_MASTER_MDP_P1,
	SDM660_MASTER_VENUS,
	SDM660_MASTER_VFE,
	SDM660_MASTER_QDSS_ETR,
	SDM660_MASTER_QDSS_BAM,
	SDM660_MASTER_SNOC_CFG,
	SDM660_MASTER_BIMC_SNOC,
	SDM660_MASTER_A2NOC_SNOC,
	SDM660_MASTER_GNOC_SNOC,

	SDM660_SLAVE_A2NOC_SNOC,
	SDM660_SLAVE_EBI,
	SDM660_SLAVE_HMSS_L3,
	SDM660_SLAVE_BIMC_SNOC,
	SDM660_SLAVE_CNOC_A2NOC,
	SDM660_SLAVE_MPM,
	SDM660_SLAVE_PMIC_ARB,
	SDM660_SLAVE_TLMM_NORTH,
	SDM660_SLAVE_TCSR,
	SDM660_SLAVE_PIMEM_CFG,
	SDM660_SLAVE_IMEM_CFG,
	SDM660_SLAVE_MESSAGE_RAM,
	SDM660_SLAVE_GLM,
	SDM660_SLAVE_BIMC_CFG,
	SDM660_SLAVE_PRNG,
	SDM660_SLAVE_SPDM,
	SDM660_SLAVE_QDSS_CFG,
	SDM660_SLAVE_CNOC_MNOC_CFG,
	SDM660_SLAVE_SNOC_CFG,
	SDM660_SLAVE_QM_CFG,
	SDM660_SLAVE_CLK_CTL,
	SDM660_SLAVE_MSS_CFG,
	SDM660_SLAVE_TLMM_SOUTH,
	SDM660_SLAVE_UFS_CFG,
	SDM660_SLAVE_A2NOC_CFG,
	SDM660_SLAVE_A2NOC_SMMU_CFG,
	SDM660_SLAVE_GPUSS_CFG,
	SDM660_SLAVE_AHB2PHY,
	SDM660_SLAVE_BLSP_1,
	SDM660_SLAVE_SDCC_1,
	SDM660_SLAVE_SDCC_2,
	SDM660_SLAVE_TLMM_CENTER,
	SDM660_SLAVE_BLSP_2,
	SDM660_SLAVE_PDM,
	SDM660_SLAVE_CNOC_MNOC_MMSS_CFG,
	SDM660_SLAVE_USB_HS,
	SDM660_SLAVE_USB3_0,
	SDM660_SLAVE_SRVC_CNOC,
	SDM660_SLAVE_GNOC_BIMC,
	SDM660_SLAVE_GNOC_SNOC,
	SDM660_SLAVE_CAMERA_CFG,
	SDM660_SLAVE_CAMERA_THROTTLE_CFG,
	SDM660_SLAVE_MISC_CFG,
	SDM660_SLAVE_VENUS_THROTTLE_CFG,
	SDM660_SLAVE_VENUS_CFG,
	SDM660_SLAVE_MMSS_CLK_XPU_CFG,
	SDM660_SLAVE_MMSS_CLK_CFG,
	SDM660_SLAVE_MNOC_MPU_CFG,
	SDM660_SLAVE_DISPLAY_CFG,
	SDM660_SLAVE_CSI_PHY_CFG,
	SDM660_SLAVE_DISPLAY_THROTTLE_CFG,
	SDM660_SLAVE_SMMU_CFG,
	SDM660_SLAVE_MNOC_BIMC,
	SDM660_SLAVE_SRVC_MNOC,
	SDM660_SLAVE_HMSS,
	SDM660_SLAVE_LPASS,
	SDM660_SLAVE_WLAN,
	SDM660_SLAVE_CDSP,
	SDM660_SLAVE_IPA,
	SDM660_SLAVE_SNOC_BIMC,
	SDM660_SLAVE_SNOC_CNOC,
	SDM660_SLAVE_IMEM,
	SDM660_SLAVE_PIMEM,
	SDM660_SLAVE_QDSS_STM,
	SDM660_SLAVE_SRVC_SNOC,

	SDM660_A2NOC,
	SDM660_BIMC,
	SDM660_CNOC,
	SDM660_GNOC,
	SDM660_MNOC,
	SDM660_SNOC,
};

static const char * const mm_intf_clocks[] = {
	"iface",
};

static const char * const a2noc_intf_clocks[] = {
	"ipa",
	"ufs_axi",
	"aggre2_ufs_axi",
	"aggre2_usb3_axi",
	"cfg_noc_usb2_axi",
};

static const u16 mas_ipa_links[] = {
	SDM660_SLAVE_A2NOC_SNOC
};

static struct qcom_icc_node mas_ipa = {
	.name = "mas_ipa",
	.id = SDM660_MASTER_IPA,
	.buswidth = 8,
	.mas_rpm_id = 59,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 3,
	.num_links = ARRAY_SIZE(mas_ipa_links),
	.links = mas_ipa_links,
};

static const u16 mas_cnoc_a2noc_links[] = {
	SDM660_SLAVE_A2NOC_SNOC
};

static struct qcom_icc_node mas_cnoc_a2noc = {
	.name = "mas_cnoc_a2noc",
	.id = SDM660_MASTER_CNOC_A2NOC,
	.buswidth = 8,
	.mas_rpm_id = 146,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_cnoc_a2noc_links),
	.links = mas_cnoc_a2noc_links,
};

static const u16 mas_sdcc_1_links[] = {
	SDM660_SLAVE_A2NOC_SNOC
};

static struct qcom_icc_node mas_sdcc_1 = {
	.name = "mas_sdcc_1",
	.id = SDM660_MASTER_SDCC_1,
	.buswidth = 8,
	.mas_rpm_id = 33,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_sdcc_1_links),
	.links = mas_sdcc_1_links,
};

static const u16 mas_sdcc_2_links[] = {
	SDM660_SLAVE_A2NOC_SNOC
};

static struct qcom_icc_node mas_sdcc_2 = {
	.name = "mas_sdcc_2",
	.id = SDM660_MASTER_SDCC_2,
	.buswidth = 8,
	.mas_rpm_id = 35,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_sdcc_2_links),
	.links = mas_sdcc_2_links,
};

static const u16 mas_blsp_1_links[] = {
	SDM660_SLAVE_A2NOC_SNOC
};

static struct qcom_icc_node mas_blsp_1 = {
	.name = "mas_blsp_1",
	.id = SDM660_MASTER_BLSP_1,
	.buswidth = 4,
	.mas_rpm_id = 41,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_blsp_1_links),
	.links = mas_blsp_1_links,
};

static const u16 mas_blsp_2_links[] = {
	SDM660_SLAVE_A2NOC_SNOC
};

static struct qcom_icc_node mas_blsp_2 = {
	.name = "mas_blsp_2",
	.id = SDM660_MASTER_BLSP_2,
	.buswidth = 4,
	.mas_rpm_id = 39,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_blsp_2_links),
	.links = mas_blsp_2_links,
};

static const u16 mas_ufs_links[] = {
	SDM660_SLAVE_A2NOC_SNOC
};

static struct qcom_icc_node mas_ufs = {
	.name = "mas_ufs",
	.id = SDM660_MASTER_UFS,
	.buswidth = 8,
	.mas_rpm_id = 68,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 4,
	.num_links = ARRAY_SIZE(mas_ufs_links),
	.links = mas_ufs_links,
};

static const u16 mas_usb_hs_links[] = {
	SDM660_SLAVE_A2NOC_SNOC
};

static struct qcom_icc_node mas_usb_hs = {
	.name = "mas_usb_hs",
	.id = SDM660_MASTER_USB_HS,
	.buswidth = 8,
	.mas_rpm_id = 42,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 1,
	.num_links = ARRAY_SIZE(mas_usb_hs_links),
	.links = mas_usb_hs_links,
};

static const u16 mas_usb3_links[] = {
	SDM660_SLAVE_A2NOC_SNOC
};

static struct qcom_icc_node mas_usb3 = {
	.name = "mas_usb3",
	.id = SDM660_MASTER_USB3,
	.buswidth = 8,
	.mas_rpm_id = 32,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 2,
	.num_links = ARRAY_SIZE(mas_usb3_links),
	.links = mas_usb3_links,
};

static const u16 mas_crypto_links[] = {
	SDM660_SLAVE_A2NOC_SNOC
};

static struct qcom_icc_node mas_crypto = {
	.name = "mas_crypto",
	.id = SDM660_MASTER_CRYPTO_C0,
	.buswidth = 8,
	.mas_rpm_id = 23,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 11,
	.num_links = ARRAY_SIZE(mas_crypto_links),
	.links = mas_crypto_links,
};

static const u16 mas_gnoc_bimc_links[] = {
	SDM660_SLAVE_EBI
};

static struct qcom_icc_node mas_gnoc_bimc = {
	.name = "mas_gnoc_bimc",
	.id = SDM660_MASTER_GNOC_BIMC,
	.buswidth = 4,
	.mas_rpm_id = 144,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 0,
	.num_links = ARRAY_SIZE(mas_gnoc_bimc_links),
	.links = mas_gnoc_bimc_links,
};

static const u16 mas_oxili_links[] = {
	SDM660_SLAVE_HMSS_L3,
	SDM660_SLAVE_EBI,
	SDM660_SLAVE_BIMC_SNOC
};

static struct qcom_icc_node mas_oxili = {
	.name = "mas_oxili",
	.id = SDM660_MASTER_OXILI,
	.buswidth = 4,
	.mas_rpm_id = 6,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 1,
	.num_links = ARRAY_SIZE(mas_oxili_links),
	.links = mas_oxili_links,
};

static const u16 mas_mnoc_bimc_links[] = {
	SDM660_SLAVE_HMSS_L3,
	SDM660_SLAVE_EBI,
	SDM660_SLAVE_BIMC_SNOC
};

static struct qcom_icc_node mas_mnoc_bimc = {
	.name = "mas_mnoc_bimc",
	.id = SDM660_MASTER_MNOC_BIMC,
	.buswidth = 4,
	.mas_rpm_id = 2,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 2,
	.num_links = ARRAY_SIZE(mas_mnoc_bimc_links),
	.links = mas_mnoc_bimc_links,
};

static const u16 mas_snoc_bimc_links[] = {
	SDM660_SLAVE_HMSS_L3,
	SDM660_SLAVE_EBI
};

static struct qcom_icc_node mas_snoc_bimc = {
	.name = "mas_snoc_bimc",
	.id = SDM660_MASTER_SNOC_BIMC,
	.buswidth = 4,
	.mas_rpm_id = 3,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_snoc_bimc_links),
	.links = mas_snoc_bimc_links,
};

static const u16 mas_pimem_links[] = {
	SDM660_SLAVE_HMSS_L3,
	SDM660_SLAVE_EBI
};

static struct qcom_icc_node mas_pimem = {
	.name = "mas_pimem",
	.id = SDM660_MASTER_PIMEM,
	.buswidth = 4,
	.mas_rpm_id = 113,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 4,
	.num_links = ARRAY_SIZE(mas_pimem_links),
	.links = mas_pimem_links,
};

static const u16 mas_snoc_cnoc_links[] = {
	SDM660_SLAVE_CLK_CTL,
	SDM660_SLAVE_QDSS_CFG,
	SDM660_SLAVE_QM_CFG,
	SDM660_SLAVE_SRVC_CNOC,
	SDM660_SLAVE_UFS_CFG,
	SDM660_SLAVE_TCSR,
	SDM660_SLAVE_A2NOC_SMMU_CFG,
	SDM660_SLAVE_SNOC_CFG,
	SDM660_SLAVE_TLMM_SOUTH,
	SDM660_SLAVE_MPM,
	SDM660_SLAVE_CNOC_MNOC_MMSS_CFG,
	SDM660_SLAVE_SDCC_2,
	SDM660_SLAVE_SDCC_1,
	SDM660_SLAVE_SPDM,
	SDM660_SLAVE_PMIC_ARB,
	SDM660_SLAVE_PRNG,
	SDM660_SLAVE_MSS_CFG,
	SDM660_SLAVE_GPUSS_CFG,
	SDM660_SLAVE_IMEM_CFG,
	SDM660_SLAVE_USB3_0,
	SDM660_SLAVE_A2NOC_CFG,
	SDM660_SLAVE_TLMM_NORTH,
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
	SDM660_SLAVE_CNOC_MNOC_CFG
};

static struct qcom_icc_node mas_snoc_cnoc = {
	.name = "mas_snoc_cnoc",
	.id = SDM660_MASTER_SNOC_CNOC,
	.buswidth = 8,
	.mas_rpm_id = 52,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_snoc_cnoc_links),
	.links = mas_snoc_cnoc_links,
};

static const u16 mas_qdss_dap_links[] = {
	SDM660_SLAVE_CLK_CTL,
	SDM660_SLAVE_QDSS_CFG,
	SDM660_SLAVE_QM_CFG,
	SDM660_SLAVE_SRVC_CNOC,
	SDM660_SLAVE_UFS_CFG,
	SDM660_SLAVE_TCSR,
	SDM660_SLAVE_A2NOC_SMMU_CFG,
	SDM660_SLAVE_SNOC_CFG,
	SDM660_SLAVE_TLMM_SOUTH,
	SDM660_SLAVE_MPM,
	SDM660_SLAVE_CNOC_MNOC_MMSS_CFG,
	SDM660_SLAVE_SDCC_2,
	SDM660_SLAVE_SDCC_1,
	SDM660_SLAVE_SPDM,
	SDM660_SLAVE_PMIC_ARB,
	SDM660_SLAVE_PRNG,
	SDM660_SLAVE_MSS_CFG,
	SDM660_SLAVE_GPUSS_CFG,
	SDM660_SLAVE_IMEM_CFG,
	SDM660_SLAVE_USB3_0,
	SDM660_SLAVE_A2NOC_CFG,
	SDM660_SLAVE_TLMM_NORTH,
	SDM660_SLAVE_USB_HS,
	SDM660_SLAVE_PDM,
	SDM660_SLAVE_TLMM_CENTER,
	SDM660_SLAVE_AHB2PHY,
	SDM660_SLAVE_BLSP_2,
	SDM660_SLAVE_BLSP_1,
	SDM660_SLAVE_PIMEM_CFG,
	SDM660_SLAVE_GLM,
	SDM660_SLAVE_MESSAGE_RAM,
	SDM660_SLAVE_CNOC_A2NOC,
	SDM660_SLAVE_BIMC_CFG,
	SDM660_SLAVE_CNOC_MNOC_CFG
};

static struct qcom_icc_node mas_qdss_dap = {
	.name = "mas_qdss_dap",
	.id = SDM660_MASTER_QDSS_DAP,
	.buswidth = 8,
	.mas_rpm_id = 49,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_qdss_dap_links),
	.links = mas_qdss_dap_links,
};

static const u16 mas_apss_proc_links[] = {
	SDM660_SLAVE_GNOC_SNOC,
	SDM660_SLAVE_GNOC_BIMC
};

static struct qcom_icc_node mas_apss_proc = {
	.name = "mas_apss_proc",
	.id = SDM660_MASTER_APPS_PROC,
	.buswidth = 16,
	.mas_rpm_id = 0,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_apss_proc_links),
	.links = mas_apss_proc_links,
};

static const u16 mas_cnoc_mnoc_mmss_cfg_links[] = {
	SDM660_SLAVE_VENUS_THROTTLE_CFG,
	SDM660_SLAVE_VENUS_CFG,
	SDM660_SLAVE_CAMERA_THROTTLE_CFG,
	SDM660_SLAVE_SMMU_CFG,
	SDM660_SLAVE_CAMERA_CFG,
	SDM660_SLAVE_CSI_PHY_CFG,
	SDM660_SLAVE_DISPLAY_THROTTLE_CFG,
	SDM660_SLAVE_DISPLAY_CFG,
	SDM660_SLAVE_MMSS_CLK_CFG,
	SDM660_SLAVE_MNOC_MPU_CFG,
	SDM660_SLAVE_MISC_CFG,
	SDM660_SLAVE_MMSS_CLK_XPU_CFG
};

static struct qcom_icc_node mas_cnoc_mnoc_mmss_cfg = {
	.name = "mas_cnoc_mnoc_mmss_cfg",
	.id = SDM660_MASTER_CNOC_MNOC_MMSS_CFG,
	.buswidth = 8,
	.mas_rpm_id = 4,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_cnoc_mnoc_mmss_cfg_links),
	.links = mas_cnoc_mnoc_mmss_cfg_links,
};

static const u16 mas_cnoc_mnoc_cfg_links[] = {
	SDM660_SLAVE_SRVC_MNOC
};

static struct qcom_icc_node mas_cnoc_mnoc_cfg = {
	.name = "mas_cnoc_mnoc_cfg",
	.id = SDM660_MASTER_CNOC_MNOC_CFG,
	.buswidth = 4,
	.mas_rpm_id = 5,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_cnoc_mnoc_cfg_links),
	.links = mas_cnoc_mnoc_cfg_links,
};

static const u16 mas_cpp_links[] = {
	SDM660_SLAVE_MNOC_BIMC
};

static struct qcom_icc_node mas_cpp = {
	.name = "mas_cpp",
	.id = SDM660_MASTER_CPP,
	.buswidth = 16,
	.mas_rpm_id = 115,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 4,
	.num_links = ARRAY_SIZE(mas_cpp_links),
	.links = mas_cpp_links,
};

static const u16 mas_jpeg_links[] = {
	SDM660_SLAVE_MNOC_BIMC
};

static struct qcom_icc_node mas_jpeg = {
	.name = "mas_jpeg",
	.id = SDM660_MASTER_JPEG,
	.buswidth = 16,
	.mas_rpm_id = 7,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 6,
	.num_links = ARRAY_SIZE(mas_jpeg_links),
	.links = mas_jpeg_links,
};

static const u16 mas_mdp_p0_links[] = {
	SDM660_SLAVE_MNOC_BIMC
};

static struct qcom_icc_node mas_mdp_p0 = {
	.name = "mas_mdp_p0",
	.id = SDM660_MASTER_MDP_P0,
	.buswidth = 16,
	.ib_coeff = 50,
	.mas_rpm_id = 8,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 0,
	.num_links = ARRAY_SIZE(mas_mdp_p0_links),
	.links = mas_mdp_p0_links,
};

static const u16 mas_mdp_p1_links[] = {
	SDM660_SLAVE_MNOC_BIMC
};

static struct qcom_icc_node mas_mdp_p1 = {
	.name = "mas_mdp_p1",
	.id = SDM660_MASTER_MDP_P1,
	.buswidth = 16,
	.ib_coeff = 50,
	.mas_rpm_id = 61,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 1,
	.num_links = ARRAY_SIZE(mas_mdp_p1_links),
	.links = mas_mdp_p1_links,
};

static const u16 mas_venus_links[] = {
	SDM660_SLAVE_MNOC_BIMC
};

static struct qcom_icc_node mas_venus = {
	.name = "mas_venus",
	.id = SDM660_MASTER_VENUS,
	.buswidth = 16,
	.mas_rpm_id = 9,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 1,
	.num_links = ARRAY_SIZE(mas_venus_links),
	.links = mas_venus_links,
};

static const u16 mas_vfe_links[] = {
	SDM660_SLAVE_MNOC_BIMC
};

static struct qcom_icc_node mas_vfe = {
	.name = "mas_vfe",
	.id = SDM660_MASTER_VFE,
	.buswidth = 16,
	.mas_rpm_id = 11,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 5,
	.num_links = ARRAY_SIZE(mas_vfe_links),
	.links = mas_vfe_links,
};

static const u16 mas_qdss_etr_links[] = {
	SDM660_SLAVE_PIMEM,
	SDM660_SLAVE_IMEM,
	SDM660_SLAVE_SNOC_CNOC,
	SDM660_SLAVE_SNOC_BIMC
};

static struct qcom_icc_node mas_qdss_etr = {
	.name = "mas_qdss_etr",
	.id = SDM660_MASTER_QDSS_ETR,
	.buswidth = 8,
	.mas_rpm_id = 31,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 1,
	.num_links = ARRAY_SIZE(mas_qdss_etr_links),
	.links = mas_qdss_etr_links,
};

static const u16 mas_qdss_bam_links[] = {
	SDM660_SLAVE_PIMEM,
	SDM660_SLAVE_IMEM,
	SDM660_SLAVE_SNOC_CNOC,
	SDM660_SLAVE_SNOC_BIMC
};

static struct qcom_icc_node mas_qdss_bam = {
	.name = "mas_qdss_bam",
	.id = SDM660_MASTER_QDSS_BAM,
	.buswidth = 4,
	.mas_rpm_id = 19,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 0,
	.num_links = ARRAY_SIZE(mas_qdss_bam_links),
	.links = mas_qdss_bam_links,
};

static const u16 mas_snoc_cfg_links[] = {
	SDM660_SLAVE_SRVC_SNOC
};

static struct qcom_icc_node mas_snoc_cfg = {
	.name = "mas_snoc_cfg",
	.id = SDM660_MASTER_SNOC_CFG,
	.buswidth = 4,
	.mas_rpm_id = 20,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_snoc_cfg_links),
	.links = mas_snoc_cfg_links,
};

static const u16 mas_bimc_snoc_links[] = {
	SDM660_SLAVE_PIMEM,
	SDM660_SLAVE_IPA,
	SDM660_SLAVE_QDSS_STM,
	SDM660_SLAVE_LPASS,
	SDM660_SLAVE_HMSS,
	SDM660_SLAVE_CDSP,
	SDM660_SLAVE_SNOC_CNOC,
	SDM660_SLAVE_WLAN,
	SDM660_SLAVE_IMEM
};

static struct qcom_icc_node mas_bimc_snoc = {
	.name = "mas_bimc_snoc",
	.id = SDM660_MASTER_BIMC_SNOC,
	.buswidth = 8,
	.mas_rpm_id = 21,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_bimc_snoc_links),
	.links = mas_bimc_snoc_links,
};

static const u16 mas_gnoc_snoc_links[] = {
	SDM660_SLAVE_PIMEM,
	SDM660_SLAVE_IPA,
	SDM660_SLAVE_QDSS_STM,
	SDM660_SLAVE_LPASS,
	SDM660_SLAVE_HMSS,
	SDM660_SLAVE_CDSP,
	SDM660_SLAVE_SNOC_CNOC,
	SDM660_SLAVE_WLAN,
	SDM660_SLAVE_IMEM
};

static struct qcom_icc_node mas_gnoc_snoc = {
	.name = "mas_gnoc_snoc",
	.id = SDM660_MASTER_GNOC_SNOC,
	.buswidth = 8,
	.mas_rpm_id = 150,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_gnoc_snoc_links),
	.links = mas_gnoc_snoc_links,
};

static const u16 mas_a2noc_snoc_links[] = {
	SDM660_SLAVE_PIMEM,
	SDM660_SLAVE_IPA,
	SDM660_SLAVE_QDSS_STM,
	SDM660_SLAVE_LPASS,
	SDM660_SLAVE_HMSS,
	SDM660_SLAVE_SNOC_BIMC,
	SDM660_SLAVE_CDSP,
	SDM660_SLAVE_SNOC_CNOC,
	SDM660_SLAVE_WLAN,
	SDM660_SLAVE_IMEM
};

static struct qcom_icc_node mas_a2noc_snoc = {
	.name = "mas_a2noc_snoc",
	.id = SDM660_MASTER_A2NOC_SNOC,
	.buswidth = 16,
	.mas_rpm_id = 112,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_a2noc_snoc_links),
	.links = mas_a2noc_snoc_links,
};

static const u16 slv_a2noc_snoc_links[] = {
	SDM660_MASTER_A2NOC_SNOC
};

static struct qcom_icc_node slv_a2noc_snoc = {
	.name = "slv_a2noc_snoc",
	.id = SDM660_SLAVE_A2NOC_SNOC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 143,
	.num_links = ARRAY_SIZE(slv_a2noc_snoc_links),
	.links = slv_a2noc_snoc_links,
};

static struct qcom_icc_node slv_ebi = {
	.name = "slv_ebi",
	.id = SDM660_SLAVE_EBI,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 0,
};

static struct qcom_icc_node slv_hmss_l3 = {
	.name = "slv_hmss_l3",
	.id = SDM660_SLAVE_HMSS_L3,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 160,
};

static const u16 slv_bimc_snoc_links[] = {
	SDM660_MASTER_BIMC_SNOC
};

static struct qcom_icc_node slv_bimc_snoc = {
	.name = "slv_bimc_snoc",
	.id = SDM660_SLAVE_BIMC_SNOC,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 2,
	.num_links = ARRAY_SIZE(slv_bimc_snoc_links),
	.links = slv_bimc_snoc_links,
};

static const u16 slv_cnoc_a2noc_links[] = {
	SDM660_MASTER_CNOC_A2NOC
};

static struct qcom_icc_node slv_cnoc_a2noc = {
	.name = "slv_cnoc_a2noc",
	.id = SDM660_SLAVE_CNOC_A2NOC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 208,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_cnoc_a2noc_links),
	.links = slv_cnoc_a2noc_links,
};

static struct qcom_icc_node slv_mpm = {
	.name = "slv_mpm",
	.id = SDM660_SLAVE_MPM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 62,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_pmic_arb = {
	.name = "slv_pmic_arb",
	.id = SDM660_SLAVE_PMIC_ARB,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 59,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_tlmm_north = {
	.name = "slv_tlmm_north",
	.id = SDM660_SLAVE_TLMM_NORTH,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 214,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_tcsr = {
	.name = "slv_tcsr",
	.id = SDM660_SLAVE_TCSR,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 50,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_pimem_cfg = {
	.name = "slv_pimem_cfg",
	.id = SDM660_SLAVE_PIMEM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 167,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_imem_cfg = {
	.name = "slv_imem_cfg",
	.id = SDM660_SLAVE_IMEM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 54,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_message_ram = {
	.name = "slv_message_ram",
	.id = SDM660_SLAVE_MESSAGE_RAM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 55,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_glm = {
	.name = "slv_glm",
	.id = SDM660_SLAVE_GLM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 209,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_bimc_cfg = {
	.name = "slv_bimc_cfg",
	.id = SDM660_SLAVE_BIMC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 56,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_prng = {
	.name = "slv_prng",
	.id = SDM660_SLAVE_PRNG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 44,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_spdm = {
	.name = "slv_spdm",
	.id = SDM660_SLAVE_SPDM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 60,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_qdss_cfg = {
	.name = "slv_qdss_cfg",
	.id = SDM660_SLAVE_QDSS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 63,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static const u16 slv_cnoc_mnoc_cfg_links[] = {
	SDM660_MASTER_CNOC_MNOC_CFG
};

static struct qcom_icc_node slv_cnoc_mnoc_cfg = {
	.name = "slv_cnoc_mnoc_cfg",
	.id = SDM660_SLAVE_CNOC_MNOC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 66,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_cnoc_mnoc_cfg_links),
	.links = slv_cnoc_mnoc_cfg_links,
};

static struct qcom_icc_node slv_snoc_cfg = {
	.name = "slv_snoc_cfg",
	.id = SDM660_SLAVE_SNOC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 70,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_qm_cfg = {
	.name = "slv_qm_cfg",
	.id = SDM660_SLAVE_QM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 212,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_clk_ctl = {
	.name = "slv_clk_ctl",
	.id = SDM660_SLAVE_CLK_CTL,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 47,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_mss_cfg = {
	.name = "slv_mss_cfg",
	.id = SDM660_SLAVE_MSS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 48,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_tlmm_south = {
	.name = "slv_tlmm_south",
	.id = SDM660_SLAVE_TLMM_SOUTH,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 217,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_ufs_cfg = {
	.name = "slv_ufs_cfg",
	.id = SDM660_SLAVE_UFS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 92,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_a2noc_cfg = {
	.name = "slv_a2noc_cfg",
	.id = SDM660_SLAVE_A2NOC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 150,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_a2noc_smmu_cfg = {
	.name = "slv_a2noc_smmu_cfg",
	.id = SDM660_SLAVE_A2NOC_SMMU_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 152,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_gpuss_cfg = {
	.name = "slv_gpuss_cfg",
	.id = SDM660_SLAVE_GPUSS_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 11,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_ahb2phy = {
	.name = "slv_ahb2phy",
	.id = SDM660_SLAVE_AHB2PHY,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 163,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_blsp_1 = {
	.name = "slv_blsp_1",
	.id = SDM660_SLAVE_BLSP_1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 39,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_sdcc_1 = {
	.name = "slv_sdcc_1",
	.id = SDM660_SLAVE_SDCC_1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 31,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_sdcc_2 = {
	.name = "slv_sdcc_2",
	.id = SDM660_SLAVE_SDCC_2,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 33,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_tlmm_center = {
	.name = "slv_tlmm_center",
	.id = SDM660_SLAVE_TLMM_CENTER,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 218,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_blsp_2 = {
	.name = "slv_blsp_2",
	.id = SDM660_SLAVE_BLSP_2,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 37,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_pdm = {
	.name = "slv_pdm",
	.id = SDM660_SLAVE_PDM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 41,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static const u16 slv_cnoc_mnoc_mmss_cfg_links[] = {
	SDM660_MASTER_CNOC_MNOC_MMSS_CFG
};

static struct qcom_icc_node slv_cnoc_mnoc_mmss_cfg = {
	.name = "slv_cnoc_mnoc_mmss_cfg",
	.id = SDM660_SLAVE_CNOC_MNOC_MMSS_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 58,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_cnoc_mnoc_mmss_cfg_links),
	.links = slv_cnoc_mnoc_mmss_cfg_links,
};

static struct qcom_icc_node slv_usb_hs = {
	.name = "slv_usb_hs",
	.id = SDM660_SLAVE_USB_HS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 40,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_usb3_0 = {
	.name = "slv_usb3_0",
	.id = SDM660_SLAVE_USB3_0,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 22,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_srvc_cnoc = {
	.name = "slv_srvc_cnoc",
	.id = SDM660_SLAVE_SRVC_CNOC,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 76,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static const u16 slv_gnoc_bimc_links[] = {
	SDM660_MASTER_GNOC_BIMC
};

static struct qcom_icc_node slv_gnoc_bimc = {
	.name = "slv_gnoc_bimc",
	.id = SDM660_SLAVE_GNOC_BIMC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 210,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_gnoc_bimc_links),
	.links = slv_gnoc_bimc_links,
};

static const u16 slv_gnoc_snoc_links[] = {
	SDM660_MASTER_GNOC_SNOC
};

static struct qcom_icc_node slv_gnoc_snoc = {
	.name = "slv_gnoc_snoc",
	.id = SDM660_SLAVE_GNOC_SNOC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 211,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_gnoc_snoc_links),
	.links = slv_gnoc_snoc_links,
};

static struct qcom_icc_node slv_camera_cfg = {
	.name = "slv_camera_cfg",
	.id = SDM660_SLAVE_CAMERA_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 3,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_camera_throttle_cfg = {
	.name = "slv_camera_throttle_cfg",
	.id = SDM660_SLAVE_CAMERA_THROTTLE_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 154,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_misc_cfg = {
	.name = "slv_misc_cfg",
	.id = SDM660_SLAVE_MISC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 8,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_venus_throttle_cfg = {
	.name = "slv_venus_throttle_cfg",
	.id = SDM660_SLAVE_VENUS_THROTTLE_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 178,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_venus_cfg = {
	.name = "slv_venus_cfg",
	.id = SDM660_SLAVE_VENUS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 10,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_mmss_clk_xpu_cfg = {
	.name = "slv_mmss_clk_xpu_cfg",
	.id = SDM660_SLAVE_MMSS_CLK_XPU_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 13,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_mmss_clk_cfg = {
	.name = "slv_mmss_clk_cfg",
	.id = SDM660_SLAVE_MMSS_CLK_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 12,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_mnoc_mpu_cfg = {
	.name = "slv_mnoc_mpu_cfg",
	.id = SDM660_SLAVE_MNOC_MPU_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 14,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_display_cfg = {
	.name = "slv_display_cfg",
	.id = SDM660_SLAVE_DISPLAY_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 4,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_csi_phy_cfg = {
	.name = "slv_csi_phy_cfg",
	.id = SDM660_SLAVE_CSI_PHY_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 224,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_display_throttle_cfg = {
	.name = "slv_display_throttle_cfg",
	.id = SDM660_SLAVE_DISPLAY_THROTTLE_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 156,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_smmu_cfg = {
	.name = "slv_smmu_cfg",
	.id = SDM660_SLAVE_SMMU_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 205,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static const u16 slv_mnoc_bimc_links[] = {
	SDM660_MASTER_MNOC_BIMC
};

static struct qcom_icc_node slv_mnoc_bimc = {
	.name = "slv_mnoc_bimc",
	.id = SDM660_SLAVE_MNOC_BIMC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 16,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_mnoc_bimc_links),
	.links = slv_mnoc_bimc_links,
};

static struct qcom_icc_node slv_srvc_mnoc = {
	.name = "slv_srvc_mnoc",
	.id = SDM660_SLAVE_SRVC_MNOC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 17,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_hmss = {
	.name = "slv_hmss",
	.id = SDM660_SLAVE_HMSS,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 20,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_lpass = {
	.name = "slv_lpass",
	.id = SDM660_SLAVE_LPASS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 21,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_wlan = {
	.name = "slv_wlan",
	.id = SDM660_SLAVE_WLAN,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 206,
};

static struct qcom_icc_node slv_cdsp = {
	.name = "slv_cdsp",
	.id = SDM660_SLAVE_CDSP,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 221,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_ipa = {
	.name = "slv_ipa",
	.id = SDM660_SLAVE_IPA,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 183,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static const u16 slv_snoc_bimc_links[] = {
	SDM660_MASTER_SNOC_BIMC
};

static struct qcom_icc_node slv_snoc_bimc = {
	.name = "slv_snoc_bimc",
	.id = SDM660_SLAVE_SNOC_BIMC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 24,
	.num_links = ARRAY_SIZE(slv_snoc_bimc_links),
	.links = slv_snoc_bimc_links,
};

static const u16 slv_snoc_cnoc_links[] = {
	SDM660_MASTER_SNOC_CNOC
};

static struct qcom_icc_node slv_snoc_cnoc = {
	.name = "slv_snoc_cnoc",
	.id = SDM660_SLAVE_SNOC_CNOC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 25,
	.num_links = ARRAY_SIZE(slv_snoc_cnoc_links),
	.links = slv_snoc_cnoc_links,
};

static struct qcom_icc_node slv_imem = {
	.name = "slv_imem",
	.id = SDM660_SLAVE_IMEM,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 26,
};

static struct qcom_icc_node slv_pimem = {
	.name = "slv_pimem",
	.id = SDM660_SLAVE_PIMEM,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 166,
};

static struct qcom_icc_node slv_qdss_stm = {
	.name = "slv_qdss_stm",
	.id = SDM660_SLAVE_QDSS_STM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 30,
};

static struct qcom_icc_node slv_srvc_snoc = {
	.name = "slv_srvc_snoc",
	.id = SDM660_SLAVE_SRVC_SNOC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 29,
};

static struct qcom_icc_node * const sdm660_a2noc_nodes[] = {
	[MASTER_IPA] = &mas_ipa,
	[MASTER_CNOC_A2NOC] = &mas_cnoc_a2noc,
	[MASTER_SDCC_1] = &mas_sdcc_1,
	[MASTER_SDCC_2] = &mas_sdcc_2,
	[MASTER_BLSP_1] = &mas_blsp_1,
	[MASTER_BLSP_2] = &mas_blsp_2,
	[MASTER_UFS] = &mas_ufs,
	[MASTER_USB_HS] = &mas_usb_hs,
	[MASTER_USB3] = &mas_usb3,
	[MASTER_CRYPTO_C0] = &mas_crypto,
	[SLAVE_A2NOC_SNOC] = &slv_a2noc_snoc,
};

static const struct regmap_config sdm660_a2noc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x20000,
	.fast_io	= true,
};

static const struct qcom_icc_desc sdm660_a2noc = {
	.type = QCOM_ICC_NOC,
	.nodes = sdm660_a2noc_nodes,
	.num_nodes = ARRAY_SIZE(sdm660_a2noc_nodes),
	.bus_clk_desc = &aggre2_clk,
	.intf_clocks = a2noc_intf_clocks,
	.num_intf_clocks = ARRAY_SIZE(a2noc_intf_clocks),
	.regmap_cfg = &sdm660_a2noc_regmap_config,
};

static struct qcom_icc_node * const sdm660_bimc_nodes[] = {
	[MASTER_GNOC_BIMC] = &mas_gnoc_bimc,
	[MASTER_OXILI] = &mas_oxili,
	[MASTER_MNOC_BIMC] = &mas_mnoc_bimc,
	[MASTER_SNOC_BIMC] = &mas_snoc_bimc,
	[MASTER_PIMEM] = &mas_pimem,
	[SLAVE_EBI] = &slv_ebi,
	[SLAVE_HMSS_L3] = &slv_hmss_l3,
	[SLAVE_BIMC_SNOC] = &slv_bimc_snoc,
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
	.nodes = sdm660_bimc_nodes,
	.num_nodes = ARRAY_SIZE(sdm660_bimc_nodes),
	.bus_clk_desc = &bimc_clk,
	.regmap_cfg = &sdm660_bimc_regmap_config,
	.ab_coeff = 153,
};

static struct qcom_icc_node * const sdm660_cnoc_nodes[] = {
	[MASTER_SNOC_CNOC] = &mas_snoc_cnoc,
	[MASTER_QDSS_DAP] = &mas_qdss_dap,
	[SLAVE_CNOC_A2NOC] = &slv_cnoc_a2noc,
	[SLAVE_MPM] = &slv_mpm,
	[SLAVE_PMIC_ARB] = &slv_pmic_arb,
	[SLAVE_TLMM_NORTH] = &slv_tlmm_north,
	[SLAVE_TCSR] = &slv_tcsr,
	[SLAVE_PIMEM_CFG] = &slv_pimem_cfg,
	[SLAVE_IMEM_CFG] = &slv_imem_cfg,
	[SLAVE_MESSAGE_RAM] = &slv_message_ram,
	[SLAVE_GLM] = &slv_glm,
	[SLAVE_BIMC_CFG] = &slv_bimc_cfg,
	[SLAVE_PRNG] = &slv_prng,
	[SLAVE_SPDM] = &slv_spdm,
	[SLAVE_QDSS_CFG] = &slv_qdss_cfg,
	[SLAVE_CNOC_MNOC_CFG] = &slv_cnoc_mnoc_cfg,
	[SLAVE_SNOC_CFG] = &slv_snoc_cfg,
	[SLAVE_QM_CFG] = &slv_qm_cfg,
	[SLAVE_CLK_CTL] = &slv_clk_ctl,
	[SLAVE_MSS_CFG] = &slv_mss_cfg,
	[SLAVE_TLMM_SOUTH] = &slv_tlmm_south,
	[SLAVE_UFS_CFG] = &slv_ufs_cfg,
	[SLAVE_A2NOC_CFG] = &slv_a2noc_cfg,
	[SLAVE_A2NOC_SMMU_CFG] = &slv_a2noc_smmu_cfg,
	[SLAVE_GPUSS_CFG] = &slv_gpuss_cfg,
	[SLAVE_AHB2PHY] = &slv_ahb2phy,
	[SLAVE_BLSP_1] = &slv_blsp_1,
	[SLAVE_SDCC_1] = &slv_sdcc_1,
	[SLAVE_SDCC_2] = &slv_sdcc_2,
	[SLAVE_TLMM_CENTER] = &slv_tlmm_center,
	[SLAVE_BLSP_2] = &slv_blsp_2,
	[SLAVE_PDM] = &slv_pdm,
	[SLAVE_CNOC_MNOC_MMSS_CFG] = &slv_cnoc_mnoc_mmss_cfg,
	[SLAVE_USB_HS] = &slv_usb_hs,
	[SLAVE_USB3_0] = &slv_usb3_0,
	[SLAVE_SRVC_CNOC] = &slv_srvc_cnoc,
};

static const struct regmap_config sdm660_cnoc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x10000,
	.fast_io	= true,
};

static const struct qcom_icc_desc sdm660_cnoc = {
	.type = QCOM_ICC_NOC,
	.nodes = sdm660_cnoc_nodes,
	.num_nodes = ARRAY_SIZE(sdm660_cnoc_nodes),
	.bus_clk_desc = &bus_2_clk,
	.regmap_cfg = &sdm660_cnoc_regmap_config,
};

static struct qcom_icc_node * const sdm660_gnoc_nodes[] = {
	[MASTER_APSS_PROC] = &mas_apss_proc,
	[SLAVE_GNOC_BIMC] = &slv_gnoc_bimc,
	[SLAVE_GNOC_SNOC] = &slv_gnoc_snoc,
};

static const struct regmap_config sdm660_gnoc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0xe000,
	.fast_io	= true,
};

static const struct qcom_icc_desc sdm660_gnoc = {
	.type = QCOM_ICC_NOC,
	.nodes = sdm660_gnoc_nodes,
	.num_nodes = ARRAY_SIZE(sdm660_gnoc_nodes),
	.regmap_cfg = &sdm660_gnoc_regmap_config,
};

static struct qcom_icc_node * const sdm660_mnoc_nodes[] = {
	[MASTER_CPP] = &mas_cpp,
	[MASTER_JPEG] = &mas_jpeg,
	[MASTER_MDP_P0] = &mas_mdp_p0,
	[MASTER_MDP_P1] = &mas_mdp_p1,
	[MASTER_VENUS] = &mas_venus,
	[MASTER_VFE] = &mas_vfe,
	[MASTER_CNOC_MNOC_MMSS_CFG] = &mas_cnoc_mnoc_mmss_cfg,
	[MASTER_CNOC_MNOC_CFG] = &mas_cnoc_mnoc_cfg,
	[SLAVE_CAMERA_CFG] = &slv_camera_cfg,
	[SLAVE_CAMERA_THROTTLE_CFG] = &slv_camera_throttle_cfg,
	[SLAVE_MISC_CFG] = &slv_misc_cfg,
	[SLAVE_VENUS_THROTTLE_CFG] = &slv_venus_throttle_cfg,
	[SLAVE_VENUS_CFG] = &slv_venus_cfg,
	[SLAVE_MMSS_CLK_XPU_CFG] = &slv_mmss_clk_xpu_cfg,
	[SLAVE_MMSS_CLK_CFG] = &slv_mmss_clk_cfg,
	[SLAVE_MNOC_MPU_CFG] = &slv_mnoc_mpu_cfg,
	[SLAVE_DISPLAY_CFG] = &slv_display_cfg,
	[SLAVE_CSI_PHY_CFG] = &slv_csi_phy_cfg,
	[SLAVE_DISPLAY_THROTTLE_CFG] = &slv_display_throttle_cfg,
	[SLAVE_SMMU_CFG] = &slv_smmu_cfg,
	[SLAVE_SRVC_MNOC] = &slv_srvc_mnoc,
	[SLAVE_MNOC_BIMC] = &slv_mnoc_bimc,
};

static const struct regmap_config sdm660_mnoc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x10000,
	.fast_io	= true,
};

static const struct qcom_icc_desc sdm660_mnoc = {
	.type = QCOM_ICC_NOC,
	.nodes = sdm660_mnoc_nodes,
	.num_nodes = ARRAY_SIZE(sdm660_mnoc_nodes),
	.bus_clk_desc = &mmaxi_0_clk,
	.intf_clocks = mm_intf_clocks,
	.num_intf_clocks = ARRAY_SIZE(mm_intf_clocks),
	.regmap_cfg = &sdm660_mnoc_regmap_config,
	.ab_coeff = 153,
};

static struct qcom_icc_node * const sdm660_snoc_nodes[] = {
	[MASTER_QDSS_ETR] = &mas_qdss_etr,
	[MASTER_QDSS_BAM] = &mas_qdss_bam,
	[MASTER_SNOC_CFG] = &mas_snoc_cfg,
	[MASTER_BIMC_SNOC] = &mas_bimc_snoc,
	[MASTER_A2NOC_SNOC] = &mas_a2noc_snoc,
	[MASTER_GNOC_SNOC] = &mas_gnoc_snoc,
	[SLAVE_HMSS] = &slv_hmss,
	[SLAVE_LPASS] = &slv_lpass,
	[SLAVE_WLAN] = &slv_wlan,
	[SLAVE_CDSP] = &slv_cdsp,
	[SLAVE_IPA] = &slv_ipa,
	[SLAVE_SNOC_BIMC] = &slv_snoc_bimc,
	[SLAVE_SNOC_CNOC] = &slv_snoc_cnoc,
	[SLAVE_IMEM] = &slv_imem,
	[SLAVE_PIMEM] = &slv_pimem,
	[SLAVE_QDSS_STM] = &slv_qdss_stm,
	[SLAVE_SRVC_SNOC] = &slv_srvc_snoc,
};

static const struct regmap_config sdm660_snoc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x20000,
	.fast_io	= true,
};

static const struct qcom_icc_desc sdm660_snoc = {
	.type = QCOM_ICC_NOC,
	.nodes = sdm660_snoc_nodes,
	.num_nodes = ARRAY_SIZE(sdm660_snoc_nodes),
	.bus_clk_desc = &bus_1_clk,
	.regmap_cfg = &sdm660_snoc_regmap_config,
};

static const struct of_device_id sdm660_noc_of_match[] = {
	{ .compatible = "qcom,sdm660-a2noc", .data = &sdm660_a2noc },
	{ .compatible = "qcom,sdm660-bimc", .data = &sdm660_bimc },
	{ .compatible = "qcom,sdm660-cnoc", .data = &sdm660_cnoc },
	{ .compatible = "qcom,sdm660-gnoc", .data = &sdm660_gnoc },
	{ .compatible = "qcom,sdm660-mnoc", .data = &sdm660_mnoc },
	{ .compatible = "qcom,sdm660-snoc", .data = &sdm660_snoc },
	{ },
};
MODULE_DEVICE_TABLE(of, sdm660_noc_of_match);

static struct platform_driver sdm660_noc_driver = {
	.probe = qnoc_probe,
	.remove = qnoc_remove,
	.driver = {
		.name = "qnoc-sdm660",
		.of_match_table = sdm660_noc_of_match,
	},
};
module_platform_driver(sdm660_noc_driver);
MODULE_DESCRIPTION("Qualcomm sdm660 NoC driver");
MODULE_LICENSE("GPL v2");
