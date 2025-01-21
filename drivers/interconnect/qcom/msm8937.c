// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on data from msm8937-bus.dtsi in Qualcomm's msm-3.18 release:
 *   Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
 */

#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/interconnect/qcom,msm8937.h>

#include "icc-rpm.h"

enum {
	QNOC_MASTER_AMPSS_M0 = 1,
	QNOC_MASTER_GRAPHICS_3D,
	QNOC_SNOC_BIMC_0_MAS,
	QNOC_SNOC_BIMC_2_MAS,
	QNOC_SNOC_BIMC_1_MAS,
	QNOC_MASTER_TCU_0,
	QNOC_MASTER_SPDM,
	QNOC_MASTER_BLSP_1,
	QNOC_MASTER_BLSP_2,
	QNOC_MASTER_USB_HS,
	QNOC_MASTER_XM_USB_HS1,
	QNOC_MASTER_CRYPTO_CORE0,
	QNOC_MASTER_SDCC_1,
	QNOC_MASTER_SDCC_2,
	QNOC_SNOC_PNOC_MAS,
	QNOC_MASTER_QDSS_BAM,
	QNOC_BIMC_SNOC_MAS,
	QNOC_MASTER_JPEG,
	QNOC_MASTER_MDP_PORT0,
	QNOC_PNOC_SNOC_MAS,
	QNOC_MASTER_VIDEO_P0,
	QNOC_MASTER_VFE,
	QNOC_MASTER_VFE1,
	QNOC_MASTER_CPP,
	QNOC_MASTER_QDSS_ETR,
	QNOC_PNOC_M_0,
	QNOC_PNOC_M_1,
	QNOC_PNOC_INT_0,
	QNOC_PNOC_INT_1,
	QNOC_PNOC_INT_2,
	QNOC_PNOC_INT_3,
	QNOC_PNOC_SLV_0,
	QNOC_PNOC_SLV_1,
	QNOC_PNOC_SLV_2,
	QNOC_PNOC_SLV_3,
	QNOC_PNOC_SLV_4,
	QNOC_PNOC_SLV_6,
	QNOC_PNOC_SLV_7,
	QNOC_PNOC_SLV_8,
	QNOC_SNOC_QDSS_INT,
	QNOC_SNOC_INT_0,
	QNOC_SNOC_INT_1,
	QNOC_SNOC_INT_2,
	QNOC_SLAVE_EBI_CH0,
	QNOC_BIMC_SNOC_SLV,
	QNOC_SLAVE_SDCC_2,
	QNOC_SLAVE_SPDM_WRAPPER,
	QNOC_SLAVE_PDM,
	QNOC_SLAVE_PRNG,
	QNOC_SLAVE_TCSR,
	QNOC_SLAVE_SNOC_CFG,
	QNOC_SLAVE_MESSAGE_RAM,
	QNOC_SLAVE_CAMERA_CFG,
	QNOC_SLAVE_DISPLAY_CFG,
	QNOC_SLAVE_VENUS_CFG,
	QNOC_SLAVE_GRAPHICS_3D_CFG,
	QNOC_SLAVE_TLMM,
	QNOC_SLAVE_BLSP_1,
	QNOC_SLAVE_BLSP_2,
	QNOC_SLAVE_PMIC_ARB,
	QNOC_SLAVE_SDCC_1,
	QNOC_SLAVE_CRYPTO_0_CFG,
	QNOC_SLAVE_USB_HS,
	QNOC_SLAVE_TCU,
	QNOC_PNOC_SNOC_SLV,
	QNOC_SLAVE_APPSS,
	QNOC_SLAVE_WCSS,
	QNOC_SNOC_BIMC_0_SLV,
	QNOC_SNOC_BIMC_1_SLV,
	QNOC_SNOC_BIMC_2_SLV,
	QNOC_SLAVE_OCIMEM,
	QNOC_SNOC_PNOC_SLV,
	QNOC_SLAVE_QDSS_STM,
	QNOC_SLAVE_CATS_128,
	QNOC_SLAVE_OCMEM_64,
	QNOC_SLAVE_LPASS,
};

static const u16 mas_apps_proc_links[] = {
	QNOC_SLAVE_EBI_CH0,
	QNOC_BIMC_SNOC_SLV
};

static struct qcom_icc_node mas_apps_proc = {
	.name = "mas_apps_proc",
	.id = QNOC_MASTER_AMPSS_M0,
	.buswidth = 8,
	.mas_rpm_id = 0,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 0,
	.num_links = ARRAY_SIZE(mas_apps_proc_links),
	.links = mas_apps_proc_links,
};

static const u16 mas_oxili_links[] = {
	QNOC_SLAVE_EBI_CH0,
	QNOC_BIMC_SNOC_SLV
};

static struct qcom_icc_node mas_oxili = {
	.name = "mas_oxili",
	.id = QNOC_MASTER_GRAPHICS_3D,
	.buswidth = 8,
	.mas_rpm_id = 6,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 2,
	.num_links = ARRAY_SIZE(mas_oxili_links),
	.links = mas_oxili_links,
};

static const u16 mas_snoc_bimc_0_links[] = {
	QNOC_SLAVE_EBI_CH0,
	QNOC_BIMC_SNOC_SLV
};

static struct qcom_icc_node mas_snoc_bimc_0 = {
	.name = "mas_snoc_bimc_0",
	.id = QNOC_SNOC_BIMC_0_MAS,
	.buswidth = 8,
	.mas_rpm_id = 3,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 3,
	.num_links = ARRAY_SIZE(mas_snoc_bimc_0_links),
	.links = mas_snoc_bimc_0_links,
};

static const u16 mas_snoc_bimc_2_links[] = {
	QNOC_SLAVE_EBI_CH0,
	QNOC_BIMC_SNOC_SLV
};

static struct qcom_icc_node mas_snoc_bimc_2 = {
	.name = "mas_snoc_bimc_2",
	.id = QNOC_SNOC_BIMC_2_MAS,
	.buswidth = 8,
	.mas_rpm_id = 108,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 4,
	.num_links = ARRAY_SIZE(mas_snoc_bimc_2_links),
	.links = mas_snoc_bimc_2_links,
};

static const u16 mas_snoc_bimc_1_links[] = {
	QNOC_SLAVE_EBI_CH0
};

static struct qcom_icc_node mas_snoc_bimc_1 = {
	.name = "mas_snoc_bimc_1",
	.id = QNOC_SNOC_BIMC_1_MAS,
	.buswidth = 8,
	.mas_rpm_id = 76,
	.slv_rpm_id = -1,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 5,
	.num_links = ARRAY_SIZE(mas_snoc_bimc_1_links),
	.links = mas_snoc_bimc_1_links,
};

static const u16 mas_tcu_0_links[] = {
	QNOC_SLAVE_EBI_CH0,
	QNOC_BIMC_SNOC_SLV
};

static struct qcom_icc_node mas_tcu_0 = {
	.name = "mas_tcu_0",
	.id = QNOC_MASTER_TCU_0,
	.buswidth = 8,
	.mas_rpm_id = 102,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.prio_level = 2,
	.qos.qos_port = 6,
	.num_links = ARRAY_SIZE(mas_tcu_0_links),
	.links = mas_tcu_0_links,
};

static const u16 mas_spdm_links[] = {
	QNOC_PNOC_M_0
};

static struct qcom_icc_node mas_spdm = {
	.name = "mas_spdm",
	.id = QNOC_MASTER_SPDM,
	.buswidth = 4,
	.mas_rpm_id = 50,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_spdm_links),
	.links = mas_spdm_links,
};

static const u16 mas_blsp_1_links[] = {
	QNOC_PNOC_M_1
};

static struct qcom_icc_node mas_blsp_1 = {
	.name = "mas_blsp_1",
	.id = QNOC_MASTER_BLSP_1,
	.buswidth = 4,
	.mas_rpm_id = 41,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_blsp_1_links),
	.links = mas_blsp_1_links,
};

static const u16 mas_blsp_2_links[] = {
	QNOC_PNOC_M_1
};

static struct qcom_icc_node mas_blsp_2 = {
	.name = "mas_blsp_2",
	.id = QNOC_MASTER_BLSP_2,
	.buswidth = 4,
	.mas_rpm_id = 39,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_blsp_2_links),
	.links = mas_blsp_2_links,
};

static const u16 mas_usb_hs1_links[] = {
	QNOC_PNOC_INT_0
};

static struct qcom_icc_node mas_usb_hs1 = {
	.name = "mas_usb_hs1",
	.id = QNOC_MASTER_USB_HS,
	.buswidth = 4,
	.mas_rpm_id = 42,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 12,
	.num_links = ARRAY_SIZE(mas_usb_hs1_links),
	.links = mas_usb_hs1_links,
};

static const u16 mas_xi_usb_hs1_links[] = {
	QNOC_PNOC_INT_0
};

static struct qcom_icc_node mas_xi_usb_hs1 = {
	.name = "mas_xi_usb_hs1",
	.id = QNOC_MASTER_XM_USB_HS1,
	.buswidth = 8,
	.mas_rpm_id = 138,
	.slv_rpm_id = -1,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 11,
	.num_links = ARRAY_SIZE(mas_xi_usb_hs1_links),
	.links = mas_xi_usb_hs1_links,
};

static const u16 mas_crypto_links[] = {
	QNOC_PNOC_INT_0
};

static struct qcom_icc_node mas_crypto = {
	.name = "mas_crypto",
	.id = QNOC_MASTER_CRYPTO_CORE0,
	.buswidth = 8,
	.mas_rpm_id = 23,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 0,
	.num_links = ARRAY_SIZE(mas_crypto_links),
	.links = mas_crypto_links,
};

static const u16 mas_sdcc_1_links[] = {
	QNOC_PNOC_INT_0
};

static struct qcom_icc_node mas_sdcc_1 = {
	.name = "mas_sdcc_1",
	.id = QNOC_MASTER_SDCC_1,
	.buswidth = 8,
	.mas_rpm_id = 33,
	.slv_rpm_id = -1,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 7,
	.num_links = ARRAY_SIZE(mas_sdcc_1_links),
	.links = mas_sdcc_1_links,
};

static const u16 mas_sdcc_2_links[] = {
	QNOC_PNOC_INT_0
};

static struct qcom_icc_node mas_sdcc_2 = {
	.name = "mas_sdcc_2",
	.id = QNOC_MASTER_SDCC_2,
	.buswidth = 8,
	.mas_rpm_id = 35,
	.slv_rpm_id = -1,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 8,
	.num_links = ARRAY_SIZE(mas_sdcc_2_links),
	.links = mas_sdcc_2_links,
};

static const u16 mas_snoc_pcnoc_links[] = {
	QNOC_PNOC_SLV_7,
	QNOC_PNOC_INT_2,
	QNOC_PNOC_INT_3
};

static struct qcom_icc_node mas_snoc_pcnoc = {
	.name = "mas_snoc_pcnoc",
	.id = QNOC_SNOC_PNOC_MAS,
	.buswidth = 8,
	.mas_rpm_id = 77,
	.slv_rpm_id = -1,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 9,
	.num_links = ARRAY_SIZE(mas_snoc_pcnoc_links),
	.links = mas_snoc_pcnoc_links,
};

static const u16 mas_qdss_bam_links[] = {
	QNOC_SNOC_QDSS_INT
};

static struct qcom_icc_node mas_qdss_bam = {
	.name = "mas_qdss_bam",
	.id = QNOC_MASTER_QDSS_BAM,
	.buswidth = 4,
	.mas_rpm_id = 19,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 11,
	.num_links = ARRAY_SIZE(mas_qdss_bam_links),
	.links = mas_qdss_bam_links,
};

static const u16 mas_bimc_snoc_links[] = {
	QNOC_SNOC_INT_0,
	QNOC_SNOC_INT_1,
	QNOC_SNOC_INT_2
};

static struct qcom_icc_node mas_bimc_snoc = {
	.name = "mas_bimc_snoc",
	.id = QNOC_BIMC_SNOC_MAS,
	.buswidth = 8,
	.mas_rpm_id = 21,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_bimc_snoc_links),
	.links = mas_bimc_snoc_links,
};

static const u16 mas_jpeg_links[] = {
	QNOC_SNOC_BIMC_2_SLV
};

static struct qcom_icc_node mas_jpeg = {
	.name = "mas_jpeg",
	.id = QNOC_MASTER_JPEG,
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

static const u16 mas_mdp_links[] = {
	QNOC_SNOC_BIMC_0_SLV
};

static struct qcom_icc_node mas_mdp = {
	.name = "mas_mdp",
	.id = QNOC_MASTER_MDP_PORT0,
	.buswidth = 16,
	.mas_rpm_id = 8,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 7,
	.num_links = ARRAY_SIZE(mas_mdp_links),
	.links = mas_mdp_links,
};

static const u16 mas_pcnoc_snoc_links[] = {
	QNOC_SNOC_INT_0,
	QNOC_SNOC_INT_1,
	QNOC_SNOC_BIMC_1_SLV
};

static struct qcom_icc_node mas_pcnoc_snoc = {
	.name = "mas_pcnoc_snoc",
	.id = QNOC_PNOC_SNOC_MAS,
	.buswidth = 8,
	.mas_rpm_id = 29,
	.slv_rpm_id = -1,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 5,
	.num_links = ARRAY_SIZE(mas_pcnoc_snoc_links),
	.links = mas_pcnoc_snoc_links,
};

static const u16 mas_venus_links[] = {
	QNOC_SNOC_BIMC_2_SLV
};

static struct qcom_icc_node mas_venus = {
	.name = "mas_venus",
	.id = QNOC_MASTER_VIDEO_P0,
	.buswidth = 16,
	.mas_rpm_id = 9,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 8,
	.num_links = ARRAY_SIZE(mas_venus_links),
	.links = mas_venus_links,
};

static const u16 mas_vfe0_links[] = {
	QNOC_SNOC_BIMC_0_SLV
};

static struct qcom_icc_node mas_vfe0 = {
	.name = "mas_vfe0",
	.id = QNOC_MASTER_VFE,
	.buswidth = 16,
	.mas_rpm_id = 11,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 9,
	.num_links = ARRAY_SIZE(mas_vfe0_links),
	.links = mas_vfe0_links,
};

static const u16 mas_vfe1_links[] = {
	QNOC_SNOC_BIMC_0_SLV
};

static struct qcom_icc_node mas_vfe1 = {
	.name = "mas_vfe1",
	.id = QNOC_MASTER_VFE1,
	.buswidth = 16,
	.mas_rpm_id = 133,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 13,
	.num_links = ARRAY_SIZE(mas_vfe1_links),
	.links = mas_vfe1_links,
};

static const u16 mas_cpp_links[] = {
	QNOC_SNOC_BIMC_2_SLV
};

static struct qcom_icc_node mas_cpp = {
	.name = "mas_cpp",
	.id = QNOC_MASTER_CPP,
	.buswidth = 16,
	.mas_rpm_id = 115,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 12,
	.num_links = ARRAY_SIZE(mas_cpp_links),
	.links = mas_cpp_links,
};

static const u16 mas_qdss_etr_links[] = {
	QNOC_SNOC_QDSS_INT
};

static struct qcom_icc_node mas_qdss_etr = {
	.name = "mas_qdss_etr",
	.id = QNOC_MASTER_QDSS_ETR,
	.buswidth = 8,
	.mas_rpm_id = 31,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 10,
	.num_links = ARRAY_SIZE(mas_qdss_etr_links),
	.links = mas_qdss_etr_links,
};

static const u16 pcnoc_m_0_links[] = {
	QNOC_PNOC_INT_0
};

static struct qcom_icc_node pcnoc_m_0 = {
	.name = "pcnoc_m_0",
	.id = QNOC_PNOC_M_0,
	.buswidth = 4,
	.mas_rpm_id = 87,
	.slv_rpm_id = 116,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 5,
	.num_links = ARRAY_SIZE(pcnoc_m_0_links),
	.links = pcnoc_m_0_links,
};

static const u16 pcnoc_m_1_links[] = {
	QNOC_PNOC_INT_0
};

static struct qcom_icc_node pcnoc_m_1 = {
	.name = "pcnoc_m_1",
	.id = QNOC_PNOC_M_1,
	.buswidth = 4,
	.mas_rpm_id = 88,
	.slv_rpm_id = 117,
	.num_links = ARRAY_SIZE(pcnoc_m_1_links),
	.links = pcnoc_m_1_links,
};

static const u16 pcnoc_int_0_links[] = {
	QNOC_PNOC_SNOC_SLV,
	QNOC_PNOC_SLV_7,
	QNOC_PNOC_INT_3,
	QNOC_PNOC_INT_2
};

static struct qcom_icc_node pcnoc_int_0 = {
	.name = "pcnoc_int_0",
	.id = QNOC_PNOC_INT_0,
	.buswidth = 8,
	.mas_rpm_id = 85,
	.slv_rpm_id = 114,
	.num_links = ARRAY_SIZE(pcnoc_int_0_links),
	.links = pcnoc_int_0_links,
};

static const u16 pcnoc_int_1_links[] = {
	QNOC_PNOC_SNOC_SLV,
	QNOC_PNOC_SLV_7,
	QNOC_PNOC_INT_3,
	QNOC_PNOC_INT_2
};

static struct qcom_icc_node pcnoc_int_1 = {
	.name = "pcnoc_int_1",
	.id = QNOC_PNOC_INT_1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(pcnoc_int_1_links),
	.links = pcnoc_int_1_links,
};

static const u16 pcnoc_int_2_links[] = {
	QNOC_PNOC_SLV_2,
	QNOC_PNOC_SLV_3,
	QNOC_PNOC_SLV_6,
	QNOC_PNOC_SLV_8
};

static struct qcom_icc_node pcnoc_int_2 = {
	.name = "pcnoc_int_2",
	.id = QNOC_PNOC_INT_2,
	.buswidth = 8,
	.mas_rpm_id = 124,
	.slv_rpm_id = 184,
	.num_links = ARRAY_SIZE(pcnoc_int_2_links),
	.links = pcnoc_int_2_links,
};

static const u16 pcnoc_int_3_links[] = {
	QNOC_PNOC_SLV_1,
	QNOC_PNOC_SLV_0,
	QNOC_PNOC_SLV_4,
	QNOC_SLAVE_GRAPHICS_3D_CFG,
	QNOC_SLAVE_TCU
};

static struct qcom_icc_node pcnoc_int_3 = {
	.name = "pcnoc_int_3",
	.id = QNOC_PNOC_INT_3,
	.buswidth = 8,
	.mas_rpm_id = 125,
	.slv_rpm_id = 185,
	.num_links = ARRAY_SIZE(pcnoc_int_3_links),
	.links = pcnoc_int_3_links,
};

static const u16 pcnoc_s_0_links[] = {
	QNOC_SLAVE_SPDM_WRAPPER,
	QNOC_SLAVE_PDM,
	QNOC_SLAVE_PRNG,
	QNOC_SLAVE_SDCC_2
};

static struct qcom_icc_node pcnoc_s_0 = {
	.name = "pcnoc_s_0",
	.id = QNOC_PNOC_SLV_0,
	.buswidth = 4,
	.mas_rpm_id = 89,
	.slv_rpm_id = 118,
	.num_links = ARRAY_SIZE(pcnoc_s_0_links),
	.links = pcnoc_s_0_links,
};

static const u16 pcnoc_s_1_links[] = {
	QNOC_SLAVE_TCSR
};

static struct qcom_icc_node pcnoc_s_1 = {
	.name = "pcnoc_s_1",
	.id = QNOC_PNOC_SLV_1,
	.buswidth = 4,
	.mas_rpm_id = 90,
	.slv_rpm_id = 119,
	.num_links = ARRAY_SIZE(pcnoc_s_1_links),
	.links = pcnoc_s_1_links,
};

static const u16 pcnoc_s_2_links[] = {
	QNOC_SLAVE_SNOC_CFG
};

static struct qcom_icc_node pcnoc_s_2 = {
	.name = "pcnoc_s_2",
	.id = QNOC_PNOC_SLV_2,
	.buswidth = 4,
	.mas_rpm_id = 91,
	.slv_rpm_id = 120,
	.num_links = ARRAY_SIZE(pcnoc_s_2_links),
	.links = pcnoc_s_2_links,
};

static const u16 pcnoc_s_3_links[] = {
	QNOC_SLAVE_MESSAGE_RAM
};

static struct qcom_icc_node pcnoc_s_3 = {
	.name = "pcnoc_s_3",
	.id = QNOC_PNOC_SLV_3,
	.buswidth = 4,
	.mas_rpm_id = 92,
	.slv_rpm_id = 121,
	.num_links = ARRAY_SIZE(pcnoc_s_3_links),
	.links = pcnoc_s_3_links,
};

static const u16 pcnoc_s_4_links[] = {
	QNOC_SLAVE_CAMERA_CFG,
	QNOC_SLAVE_DISPLAY_CFG,
	QNOC_SLAVE_VENUS_CFG
};

static struct qcom_icc_node pcnoc_s_4 = {
	.name = "pcnoc_s_4",
	.id = QNOC_PNOC_SLV_4,
	.buswidth = 4,
	.mas_rpm_id = 93,
	.slv_rpm_id = 122,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(pcnoc_s_4_links),
	.links = pcnoc_s_4_links,
};

static const u16 pcnoc_s_6_links[] = {
	QNOC_SLAVE_TLMM,
	QNOC_SLAVE_BLSP_1,
	QNOC_SLAVE_BLSP_2
};

static struct qcom_icc_node pcnoc_s_6 = {
	.name = "pcnoc_s_6",
	.id = QNOC_PNOC_SLV_6,
	.buswidth = 4,
	.mas_rpm_id = 94,
	.slv_rpm_id = 123,
	.num_links = ARRAY_SIZE(pcnoc_s_6_links),
	.links = pcnoc_s_6_links,
};

static const u16 pcnoc_s_7_links[] = {
	QNOC_SLAVE_SDCC_1,
	QNOC_SLAVE_PMIC_ARB
};

static struct qcom_icc_node pcnoc_s_7 = {
	.name = "pcnoc_s_7",
	.id = QNOC_PNOC_SLV_7,
	.buswidth = 4,
	.mas_rpm_id = 95,
	.slv_rpm_id = 124,
	.num_links = ARRAY_SIZE(pcnoc_s_7_links),
	.links = pcnoc_s_7_links,
};

static const u16 pcnoc_s_8_links[] = {
	QNOC_SLAVE_USB_HS,
	QNOC_SLAVE_CRYPTO_0_CFG
};

static struct qcom_icc_node pcnoc_s_8 = {
	.name = "pcnoc_s_8",
	.id = QNOC_PNOC_SLV_8,
	.buswidth = 4,
	.mas_rpm_id = 96,
	.slv_rpm_id = 125,
	.num_links = ARRAY_SIZE(pcnoc_s_8_links),
	.links = pcnoc_s_8_links,
};

static const u16 qdss_int_links[] = {
	QNOC_SNOC_INT_1,
	QNOC_SNOC_BIMC_1_SLV
};

static struct qcom_icc_node qdss_int = {
	.name = "qdss_int",
	.id = QNOC_SNOC_QDSS_INT,
	.buswidth = 8,
	.mas_rpm_id = 98,
	.slv_rpm_id = 128,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(qdss_int_links),
	.links = qdss_int_links,
};

static const u16 snoc_int_0_links[] = {
	QNOC_SLAVE_LPASS,
	QNOC_SLAVE_WCSS,
	QNOC_SLAVE_APPSS
};

static struct qcom_icc_node snoc_int_0 = {
	.name = "snoc_int_0",
	.id = QNOC_SNOC_INT_0,
	.buswidth = 8,
	.mas_rpm_id = 99,
	.slv_rpm_id = 130,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(snoc_int_0_links),
	.links = snoc_int_0_links,
};

static const u16 snoc_int_1_links[] = {
	QNOC_SLAVE_QDSS_STM,
	QNOC_SLAVE_OCIMEM,
	QNOC_SNOC_PNOC_SLV
};

static struct qcom_icc_node snoc_int_1 = {
	.name = "snoc_int_1",
	.id = QNOC_SNOC_INT_1,
	.buswidth = 8,
	.mas_rpm_id = 100,
	.slv_rpm_id = 131,
	.num_links = ARRAY_SIZE(snoc_int_1_links),
	.links = snoc_int_1_links,
};

static const u16 snoc_int_2_links[] = {
	QNOC_SLAVE_CATS_128,
	QNOC_SLAVE_OCMEM_64
};

static struct qcom_icc_node snoc_int_2 = {
	.name = "snoc_int_2",
	.id = QNOC_SNOC_INT_2,
	.buswidth = 8,
	.mas_rpm_id = 134,
	.slv_rpm_id = 197,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(snoc_int_2_links),
	.links = snoc_int_2_links,
};

static struct qcom_icc_node slv_ebi = {
	.name = "slv_ebi",
	.id = QNOC_SLAVE_EBI_CH0,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 0,
};

static const u16 slv_bimc_snoc_links[] = {
	QNOC_BIMC_SNOC_MAS
};

static struct qcom_icc_node slv_bimc_snoc = {
	.name = "slv_bimc_snoc",
	.id = QNOC_BIMC_SNOC_SLV,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 2,
	.num_links = ARRAY_SIZE(slv_bimc_snoc_links),
	.links = slv_bimc_snoc_links,
};

static struct qcom_icc_node slv_sdcc_2 = {
	.name = "slv_sdcc_2",
	.id = QNOC_SLAVE_SDCC_2,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 33,
};

static struct qcom_icc_node slv_spdm = {
	.name = "slv_spdm",
	.id = QNOC_SLAVE_SPDM_WRAPPER,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 60,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_pdm = {
	.name = "slv_pdm",
	.id = QNOC_SLAVE_PDM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 41,
};

static struct qcom_icc_node slv_prng = {
	.name = "slv_prng",
	.id = QNOC_SLAVE_PRNG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 44,
};

static struct qcom_icc_node slv_tcsr = {
	.name = "slv_tcsr",
	.id = QNOC_SLAVE_TCSR,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 50,
};

static struct qcom_icc_node slv_snoc_cfg = {
	.name = "slv_snoc_cfg",
	.id = QNOC_SLAVE_SNOC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 70,
};

static struct qcom_icc_node slv_message_ram = {
	.name = "slv_message_ram",
	.id = QNOC_SLAVE_MESSAGE_RAM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 55,
};

static struct qcom_icc_node slv_camera_ss_cfg = {
	.name = "slv_camera_ss_cfg",
	.id = QNOC_SLAVE_CAMERA_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 3,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_disp_ss_cfg = {
	.name = "slv_disp_ss_cfg",
	.id = QNOC_SLAVE_DISPLAY_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 4,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_venus_cfg = {
	.name = "slv_venus_cfg",
	.id = QNOC_SLAVE_VENUS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 10,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_gpu_cfg = {
	.name = "slv_gpu_cfg",
	.id = QNOC_SLAVE_GRAPHICS_3D_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 11,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_tlmm = {
	.name = "slv_tlmm",
	.id = QNOC_SLAVE_TLMM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 51,
};

static struct qcom_icc_node slv_blsp_1 = {
	.name = "slv_blsp_1",
	.id = QNOC_SLAVE_BLSP_1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 39,
};

static struct qcom_icc_node slv_blsp_2 = {
	.name = "slv_blsp_2",
	.id = QNOC_SLAVE_BLSP_2,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 37,
};

static struct qcom_icc_node slv_pmic_arb = {
	.name = "slv_pmic_arb",
	.id = QNOC_SLAVE_PMIC_ARB,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 59,
};

static struct qcom_icc_node slv_sdcc_1 = {
	.name = "slv_sdcc_1",
	.id = QNOC_SLAVE_SDCC_1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 31,
};

static struct qcom_icc_node slv_crypto_0_cfg = {
	.name = "slv_crypto_0_cfg",
	.id = QNOC_SLAVE_CRYPTO_0_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 52,
};

static struct qcom_icc_node slv_usb_hs = {
	.name = "slv_usb_hs",
	.id = QNOC_SLAVE_USB_HS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 40,
};

static struct qcom_icc_node slv_tcu = {
	.name = "slv_tcu",
	.id = QNOC_SLAVE_TCU,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 133,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static const u16 slv_pcnoc_snoc_links[] = {
	QNOC_PNOC_SNOC_MAS
};

static struct qcom_icc_node slv_pcnoc_snoc = {
	.name = "slv_pcnoc_snoc",
	.id = QNOC_PNOC_SNOC_SLV,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 45,
	.num_links = ARRAY_SIZE(slv_pcnoc_snoc_links),
	.links = slv_pcnoc_snoc_links,
};

static struct qcom_icc_node slv_kpss_ahb = {
	.name = "slv_kpss_ahb",
	.id = QNOC_SLAVE_APPSS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 20,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_wcss = {
	.name = "slv_wcss",
	.id = QNOC_SLAVE_WCSS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 23,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static const u16 slv_snoc_bimc_0_links[] = {
	QNOC_SNOC_BIMC_0_MAS
};

static struct qcom_icc_node slv_snoc_bimc_0 = {
	.name = "slv_snoc_bimc_0",
	.id = QNOC_SNOC_BIMC_0_SLV,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 24,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_snoc_bimc_0_links),
	.links = slv_snoc_bimc_0_links,
};

static const u16 slv_snoc_bimc_1_links[] = {
	QNOC_SNOC_BIMC_1_MAS
};

static struct qcom_icc_node slv_snoc_bimc_1 = {
	.name = "slv_snoc_bimc_1",
	.id = QNOC_SNOC_BIMC_1_SLV,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 104,
	.num_links = ARRAY_SIZE(slv_snoc_bimc_1_links),
	.links = slv_snoc_bimc_1_links,
};

static const u16 slv_snoc_bimc_2_links[] = {
	QNOC_SNOC_BIMC_2_MAS
};

static struct qcom_icc_node slv_snoc_bimc_2 = {
	.name = "slv_snoc_bimc_2",
	.id = QNOC_SNOC_BIMC_2_SLV,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 137,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_snoc_bimc_2_links),
	.links = slv_snoc_bimc_2_links,
};

static struct qcom_icc_node slv_imem = {
	.name = "slv_imem",
	.id = QNOC_SLAVE_OCIMEM,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 26,
};

static const u16 slv_snoc_pcnoc_links[] = {
	QNOC_SNOC_PNOC_MAS
};

static struct qcom_icc_node slv_snoc_pcnoc = {
	.name = "slv_snoc_pcnoc",
	.id = QNOC_SNOC_PNOC_SLV,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 28,
	.num_links = ARRAY_SIZE(slv_snoc_pcnoc_links),
	.links = slv_snoc_pcnoc_links,
};

static struct qcom_icc_node slv_qdss_stm = {
	.name = "slv_qdss_stm",
	.id = QNOC_SLAVE_QDSS_STM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 30,
};

static struct qcom_icc_node slv_cats_0 = {
	.name = "slv_cats_0",
	.id = QNOC_SLAVE_CATS_128,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 106,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_cats_1 = {
	.name = "slv_cats_1",
	.id = QNOC_SLAVE_OCMEM_64,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 107,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node slv_lpass = {
	.name = "slv_lpass",
	.id = QNOC_SLAVE_LPASS,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 21,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
};

static struct qcom_icc_node * const msm8937_bimc_nodes[] = {
	[MAS_APPS_PROC] = &mas_apps_proc,
	[MAS_OXILI] = &mas_oxili,
	[MAS_SNOC_BIMC_0] = &mas_snoc_bimc_0,
	[MAS_SNOC_BIMC_2] = &mas_snoc_bimc_2,
	[MAS_SNOC_BIMC_1] = &mas_snoc_bimc_1,
	[MAS_TCU_0] = &mas_tcu_0,
	[SLV_EBI] = &slv_ebi,
	[SLV_BIMC_SNOC] = &slv_bimc_snoc,
};

static const struct regmap_config msm8937_bimc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x5A000,
	.fast_io = true,
};

static const struct qcom_icc_desc msm8937_bimc = {
	.type = QCOM_ICC_BIMC,
	.nodes = msm8937_bimc_nodes,
	.num_nodes = ARRAY_SIZE(msm8937_bimc_nodes),
	.bus_clk_desc = &bimc_clk,
	.regmap_cfg = &msm8937_bimc_regmap_config,
	.qos_offset = 0x8000,
	.ab_coeff = 154,
};

static struct qcom_icc_node * const msm8937_pcnoc_nodes[] = {
	[MAS_SPDM] = &mas_spdm,
	[MAS_BLSP_1] = &mas_blsp_1,
	[MAS_BLSP_2] = &mas_blsp_2,
	[MAS_USB_HS1] = &mas_usb_hs1,
	[MAS_XI_USB_HS1] = &mas_xi_usb_hs1,
	[MAS_CRYPTO] = &mas_crypto,
	[MAS_SDCC_1] = &mas_sdcc_1,
	[MAS_SDCC_2] = &mas_sdcc_2,
	[MAS_SNOC_PCNOC] = &mas_snoc_pcnoc,
	[PCNOC_M_0] = &pcnoc_m_0,
	[PCNOC_M_1] = &pcnoc_m_1,
	[PCNOC_INT_0] = &pcnoc_int_0,
	[PCNOC_INT_1] = &pcnoc_int_1,
	[PCNOC_INT_2] = &pcnoc_int_2,
	[PCNOC_INT_3] = &pcnoc_int_3,
	[PCNOC_S_0] = &pcnoc_s_0,
	[PCNOC_S_1] = &pcnoc_s_1,
	[PCNOC_S_2] = &pcnoc_s_2,
	[PCNOC_S_3] = &pcnoc_s_3,
	[PCNOC_S_4] = &pcnoc_s_4,
	[PCNOC_S_6] = &pcnoc_s_6,
	[PCNOC_S_7] = &pcnoc_s_7,
	[PCNOC_S_8] = &pcnoc_s_8,
	[SLV_SDCC_2] = &slv_sdcc_2,
	[SLV_SPDM] = &slv_spdm,
	[SLV_PDM] = &slv_pdm,
	[SLV_PRNG] = &slv_prng,
	[SLV_TCSR] = &slv_tcsr,
	[SLV_SNOC_CFG] = &slv_snoc_cfg,
	[SLV_MESSAGE_RAM] = &slv_message_ram,
	[SLV_CAMERA_SS_CFG] = &slv_camera_ss_cfg,
	[SLV_DISP_SS_CFG] = &slv_disp_ss_cfg,
	[SLV_VENUS_CFG] = &slv_venus_cfg,
	[SLV_GPU_CFG] = &slv_gpu_cfg,
	[SLV_TLMM] = &slv_tlmm,
	[SLV_BLSP_1] = &slv_blsp_1,
	[SLV_BLSP_2] = &slv_blsp_2,
	[SLV_PMIC_ARB] = &slv_pmic_arb,
	[SLV_SDCC_1] = &slv_sdcc_1,
	[SLV_CRYPTO_0_CFG] = &slv_crypto_0_cfg,
	[SLV_USB_HS] = &slv_usb_hs,
	[SLV_TCU] = &slv_tcu,
	[SLV_PCNOC_SNOC] = &slv_pcnoc_snoc,
};

static const struct regmap_config msm8937_pcnoc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x13080,
	.fast_io = true,
};

static const struct qcom_icc_desc msm8937_pcnoc = {
	.type = QCOM_ICC_NOC,
	.nodes = msm8937_pcnoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8937_pcnoc_nodes),
	.bus_clk_desc = &bus_0_clk,
	.qos_offset = 0x7000,
	.keep_alive = true,
	.regmap_cfg = &msm8937_pcnoc_regmap_config,
};

static struct qcom_icc_node * const msm8937_snoc_nodes[] = {
	[MAS_QDSS_BAM] = &mas_qdss_bam,
	[MAS_BIMC_SNOC] = &mas_bimc_snoc,
	[MAS_PCNOC_SNOC] = &mas_pcnoc_snoc,
	[MAS_QDSS_ETR] = &mas_qdss_etr,
	[QDSS_INT] = &qdss_int,
	[SNOC_INT_0] = &snoc_int_0,
	[SNOC_INT_1] = &snoc_int_1,
	[SNOC_INT_2] = &snoc_int_2,
	[SLV_KPSS_AHB] = &slv_kpss_ahb,
	[SLV_WCSS] = &slv_wcss,
	[SLV_SNOC_BIMC_1] = &slv_snoc_bimc_1,
	[SLV_IMEM] = &slv_imem,
	[SLV_SNOC_PCNOC] = &slv_snoc_pcnoc,
	[SLV_QDSS_STM] = &slv_qdss_stm,
	[SLV_CATS_1] = &slv_cats_1,
	[SLV_LPASS] = &slv_lpass,
};

static const struct regmap_config msm8937_snoc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x16080,
	.fast_io = true,
};

static const struct qcom_icc_desc msm8937_snoc = {
	.type = QCOM_ICC_NOC,
	.nodes = msm8937_snoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8937_snoc_nodes),
	.bus_clk_desc = &bus_1_clk,
	.regmap_cfg = &msm8937_snoc_regmap_config,
	.qos_offset = 0x7000,
};

static struct qcom_icc_node * const msm8937_snoc_mm_nodes[] = {
	[MAS_JPEG] = &mas_jpeg,
	[MAS_MDP] = &mas_mdp,
	[MAS_VENUS] = &mas_venus,
	[MAS_VFE0] = &mas_vfe0,
	[MAS_VFE1] = &mas_vfe1,
	[MAS_CPP] = &mas_cpp,
	[SLV_SNOC_BIMC_0] = &slv_snoc_bimc_0,
	[SLV_SNOC_BIMC_2] = &slv_snoc_bimc_2,
	[SLV_CATS_0] = &slv_cats_0,
};

static const struct qcom_icc_desc msm8937_snoc_mm = {
	.type = QCOM_ICC_NOC,
	.nodes = msm8937_snoc_mm_nodes,
	.num_nodes = ARRAY_SIZE(msm8937_snoc_mm_nodes),
	.bus_clk_desc = &bus_2_clk,
	.regmap_cfg = &msm8937_snoc_regmap_config,
	.qos_offset = 0x7000,
	.ab_coeff = 154,
};

static const struct of_device_id msm8937_noc_of_match[] = {
	{ .compatible = "qcom,msm8937-bimc", .data = &msm8937_bimc },
	{ .compatible = "qcom,msm8937-pcnoc", .data = &msm8937_pcnoc },
	{ .compatible = "qcom,msm8937-snoc", .data = &msm8937_snoc },
	{ .compatible = "qcom,msm8937-snoc-mm", .data = &msm8937_snoc_mm },
	{ }
};
MODULE_DEVICE_TABLE(of, msm8937_noc_of_match);

static struct platform_driver msm8937_noc_driver = {
	.probe = qnoc_probe,
	.remove = qnoc_remove,
	.driver = {
		.name = "qnoc-msm8937",
		.of_match_table = msm8937_noc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(msm8937_noc_driver);

MODULE_DESCRIPTION("Qualcomm MSM8937 NoC driver");
MODULE_LICENSE("GPL");
