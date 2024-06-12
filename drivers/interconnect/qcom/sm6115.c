// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2023, Linaro Limited
 */

#include <dt-bindings/interconnect/qcom,sm6115.h>
#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "icc-rpm.h"

static const char * const snoc_intf_clocks[] = {
	"cpu_axi",
	"ufs_axi",
	"usb_axi",
	"ipa", /* Required by qxm_ipa */
};

static const char * const cnoc_intf_clocks[] = {
	"usb_axi",
};

enum {
	SM6115_MASTER_AMPSS_M0,
	SM6115_MASTER_ANOC_SNOC,
	SM6115_MASTER_BIMC_SNOC,
	SM6115_MASTER_CAMNOC_HF,
	SM6115_MASTER_CAMNOC_SF,
	SM6115_MASTER_CRYPTO_CORE0,
	SM6115_MASTER_GRAPHICS_3D,
	SM6115_MASTER_IPA,
	SM6115_MASTER_MDP_PORT0,
	SM6115_MASTER_PIMEM,
	SM6115_MASTER_QDSS_BAM,
	SM6115_MASTER_QDSS_DAP,
	SM6115_MASTER_QDSS_ETR,
	SM6115_MASTER_QPIC,
	SM6115_MASTER_QUP_0,
	SM6115_MASTER_QUP_CORE_0,
	SM6115_MASTER_SDCC_1,
	SM6115_MASTER_SDCC_2,
	SM6115_MASTER_SNOC_BIMC_NRT,
	SM6115_MASTER_SNOC_BIMC_RT,
	SM6115_MASTER_SNOC_BIMC,
	SM6115_MASTER_SNOC_CFG,
	SM6115_MASTER_SNOC_CNOC,
	SM6115_MASTER_TCU_0,
	SM6115_MASTER_TIC,
	SM6115_MASTER_USB3,
	SM6115_MASTER_VIDEO_P0,
	SM6115_MASTER_VIDEO_PROC,

	SM6115_SLAVE_AHB2PHY_USB,
	SM6115_SLAVE_ANOC_SNOC,
	SM6115_SLAVE_APPSS,
	SM6115_SLAVE_APSS_THROTTLE_CFG,
	SM6115_SLAVE_BIMC_CFG,
	SM6115_SLAVE_BIMC_SNOC,
	SM6115_SLAVE_BOOT_ROM,
	SM6115_SLAVE_CAMERA_CFG,
	SM6115_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	SM6115_SLAVE_CAMERA_RT_THROTTLE_CFG,
	SM6115_SLAVE_CLK_CTL,
	SM6115_SLAVE_CNOC_MSS,
	SM6115_SLAVE_CRYPTO_0_CFG,
	SM6115_SLAVE_DCC_CFG,
	SM6115_SLAVE_DDR_PHY_CFG,
	SM6115_SLAVE_DDR_SS_CFG,
	SM6115_SLAVE_DISPLAY_CFG,
	SM6115_SLAVE_DISPLAY_THROTTLE_CFG,
	SM6115_SLAVE_EBI_CH0,
	SM6115_SLAVE_GPU_CFG,
	SM6115_SLAVE_GPU_THROTTLE_CFG,
	SM6115_SLAVE_HWKM_CORE,
	SM6115_SLAVE_IMEM_CFG,
	SM6115_SLAVE_IPA_CFG,
	SM6115_SLAVE_LPASS,
	SM6115_SLAVE_MAPSS,
	SM6115_SLAVE_MDSP_MPU_CFG,
	SM6115_SLAVE_MESSAGE_RAM,
	SM6115_SLAVE_OCIMEM,
	SM6115_SLAVE_PDM,
	SM6115_SLAVE_PIMEM_CFG,
	SM6115_SLAVE_PIMEM,
	SM6115_SLAVE_PKA_CORE,
	SM6115_SLAVE_PMIC_ARB,
	SM6115_SLAVE_QDSS_CFG,
	SM6115_SLAVE_QDSS_STM,
	SM6115_SLAVE_QM_CFG,
	SM6115_SLAVE_QM_MPU_CFG,
	SM6115_SLAVE_QPIC,
	SM6115_SLAVE_QUP_0,
	SM6115_SLAVE_QUP_CORE_0,
	SM6115_SLAVE_RBCPR_CX_CFG,
	SM6115_SLAVE_RBCPR_MX_CFG,
	SM6115_SLAVE_RPM,
	SM6115_SLAVE_SDCC_1,
	SM6115_SLAVE_SDCC_2,
	SM6115_SLAVE_SECURITY,
	SM6115_SLAVE_SERVICE_CNOC,
	SM6115_SLAVE_SERVICE_SNOC,
	SM6115_SLAVE_SNOC_BIMC_NRT,
	SM6115_SLAVE_SNOC_BIMC_RT,
	SM6115_SLAVE_SNOC_BIMC,
	SM6115_SLAVE_SNOC_CFG,
	SM6115_SLAVE_SNOC_CNOC,
	SM6115_SLAVE_TCSR,
	SM6115_SLAVE_TCU,
	SM6115_SLAVE_TLMM,
	SM6115_SLAVE_USB3,
	SM6115_SLAVE_VENUS_CFG,
	SM6115_SLAVE_VENUS_THROTTLE_CFG,
	SM6115_SLAVE_VSENSE_CTRL_CFG,
};

static const u16 slv_ebi_slv_bimc_snoc_links[] = {
	SM6115_SLAVE_EBI_CH0,
	SM6115_SLAVE_BIMC_SNOC,
};

static struct qcom_icc_node apps_proc = {
	.name = "apps_proc",
	.id = SM6115_MASTER_AMPSS_M0,
	.channels = 1,
	.buswidth = 16,
	.qos.qos_port = 0,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.prio_level = 0,
	.qos.areq_prio = 0,
	.mas_rpm_id = 0,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(slv_ebi_slv_bimc_snoc_links),
	.links = slv_ebi_slv_bimc_snoc_links,
};

static const u16 link_slv_ebi[] = {
	SM6115_SLAVE_EBI_CH0,
};

static struct qcom_icc_node mas_snoc_bimc_rt = {
	.name = "mas_snoc_bimc_rt",
	.id = SM6115_MASTER_SNOC_BIMC_RT,
	.channels = 1,
	.buswidth = 16,
	.qos.qos_port = 2,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_ebi),
	.links = link_slv_ebi,
};

static struct qcom_icc_node mas_snoc_bimc_nrt = {
	.name = "mas_snoc_bimc_nrt",
	.id = SM6115_MASTER_SNOC_BIMC_NRT,
	.channels = 1,
	.buswidth = 16,
	.qos.qos_port = 3,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_ebi),
	.links = link_slv_ebi,
};

static struct qcom_icc_node mas_snoc_bimc = {
	.name = "mas_snoc_bimc",
	.id = SM6115_MASTER_SNOC_BIMC,
	.channels = 1,
	.buswidth = 16,
	.qos.qos_port = 6,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.mas_rpm_id = 3,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_ebi),
	.links = link_slv_ebi,
};

static struct qcom_icc_node qnm_gpu = {
	.name = "qnm_gpu",
	.id = SM6115_MASTER_GRAPHICS_3D,
	.channels = 1,
	.buswidth = 32,
	.qos.qos_port = 1,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.prio_level = 0,
	.qos.areq_prio = 0,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(slv_ebi_slv_bimc_snoc_links),
	.links = slv_ebi_slv_bimc_snoc_links,
};

static struct qcom_icc_node tcu_0 = {
	.name = "tcu_0",
	.id = SM6115_MASTER_TCU_0,
	.channels = 1,
	.buswidth = 8,
	.qos.qos_port = 4,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.prio_level = 6,
	.qos.areq_prio = 6,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(slv_ebi_slv_bimc_snoc_links),
	.links = slv_ebi_slv_bimc_snoc_links,
};

static const u16 qup_core_0_links[] = {
	SM6115_SLAVE_QUP_CORE_0,
};

static struct qcom_icc_node qup0_core_master = {
	.name = "qup0_core_master",
	.id = SM6115_MASTER_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = 170,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qup_core_0_links),
	.links = qup_core_0_links,
};

static const u16 link_slv_anoc_snoc[] = {
	SM6115_SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node crypto_c0 = {
	.name = "crypto_c0",
	.id = SM6115_MASTER_CRYPTO_CORE0,
	.channels = 1,
	.buswidth = 8,
	.qos.qos_port = 22,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = 23,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_anoc_snoc),
	.links = link_slv_anoc_snoc,
};

static const u16 mas_snoc_cnoc_links[] = {
	SM6115_SLAVE_AHB2PHY_USB,
	SM6115_SLAVE_APSS_THROTTLE_CFG,
	SM6115_SLAVE_BIMC_CFG,
	SM6115_SLAVE_BOOT_ROM,
	SM6115_SLAVE_CAMERA_CFG,
	SM6115_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	SM6115_SLAVE_CAMERA_RT_THROTTLE_CFG,
	SM6115_SLAVE_CLK_CTL,
	SM6115_SLAVE_CNOC_MSS,
	SM6115_SLAVE_CRYPTO_0_CFG,
	SM6115_SLAVE_DCC_CFG,
	SM6115_SLAVE_DDR_PHY_CFG,
	SM6115_SLAVE_DDR_SS_CFG,
	SM6115_SLAVE_DISPLAY_CFG,
	SM6115_SLAVE_DISPLAY_THROTTLE_CFG,
	SM6115_SLAVE_GPU_CFG,
	SM6115_SLAVE_GPU_THROTTLE_CFG,
	SM6115_SLAVE_HWKM_CORE,
	SM6115_SLAVE_IMEM_CFG,
	SM6115_SLAVE_IPA_CFG,
	SM6115_SLAVE_LPASS,
	SM6115_SLAVE_MAPSS,
	SM6115_SLAVE_MDSP_MPU_CFG,
	SM6115_SLAVE_MESSAGE_RAM,
	SM6115_SLAVE_PDM,
	SM6115_SLAVE_PIMEM_CFG,
	SM6115_SLAVE_PKA_CORE,
	SM6115_SLAVE_PMIC_ARB,
	SM6115_SLAVE_QDSS_CFG,
	SM6115_SLAVE_QM_CFG,
	SM6115_SLAVE_QM_MPU_CFG,
	SM6115_SLAVE_QPIC,
	SM6115_SLAVE_QUP_0,
	SM6115_SLAVE_RBCPR_CX_CFG,
	SM6115_SLAVE_RBCPR_MX_CFG,
	SM6115_SLAVE_RPM,
	SM6115_SLAVE_SDCC_1,
	SM6115_SLAVE_SDCC_2,
	SM6115_SLAVE_SECURITY,
	SM6115_SLAVE_SERVICE_CNOC,
	SM6115_SLAVE_SNOC_CFG,
	SM6115_SLAVE_TCSR,
	SM6115_SLAVE_TLMM,
	SM6115_SLAVE_USB3,
	SM6115_SLAVE_VENUS_CFG,
	SM6115_SLAVE_VENUS_THROTTLE_CFG,
	SM6115_SLAVE_VSENSE_CTRL_CFG,
};

static struct qcom_icc_node mas_snoc_cnoc = {
	.name = "mas_snoc_cnoc",
	.id = SM6115_MASTER_SNOC_CNOC,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_snoc_cnoc_links),
	.links = mas_snoc_cnoc_links,
};

static struct qcom_icc_node xm_dap = {
	.name = "xm_dap",
	.id = SM6115_MASTER_QDSS_DAP,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_snoc_cnoc_links),
	.links = mas_snoc_cnoc_links,
};

static const u16 link_slv_snoc_bimc_nrt[] = {
	SM6115_SLAVE_SNOC_BIMC_NRT,
};

static struct qcom_icc_node qnm_camera_nrt = {
	.name = "qnm_camera_nrt",
	.id = SM6115_MASTER_CAMNOC_SF,
	.channels = 1,
	.buswidth = 32,
	.qos.qos_port = 4,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 3,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_snoc_bimc_nrt),
	.links = link_slv_snoc_bimc_nrt,
};

static struct qcom_icc_node qxm_venus0 = {
	.name = "qxm_venus0",
	.id = SM6115_MASTER_VIDEO_P0,
	.channels = 1,
	.buswidth = 16,
	.qos.qos_port = 9,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 3,
	.qos.urg_fwd_en = true,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_snoc_bimc_nrt),
	.links = link_slv_snoc_bimc_nrt,
};

static struct qcom_icc_node qxm_venus_cpu = {
	.name = "qxm_venus_cpu",
	.id = SM6115_MASTER_VIDEO_PROC,
	.channels = 1,
	.buswidth = 8,
	.qos.qos_port = 13,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_snoc_bimc_nrt),
	.links = link_slv_snoc_bimc_nrt,
};

static const u16 link_slv_snoc_bimc_rt[] = {
	SM6115_SLAVE_SNOC_BIMC_RT,
};

static struct qcom_icc_node qnm_camera_rt = {
	.name = "qnm_camera_rt",
	.id = SM6115_MASTER_CAMNOC_HF,
	.channels = 1,
	.buswidth = 32,
	.qos.qos_port = 10,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 3,
	.qos.urg_fwd_en = true,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_snoc_bimc_rt),
	.links = link_slv_snoc_bimc_rt,
};

static struct qcom_icc_node qxm_mdp0 = {
	.name = "qxm_mdp0",
	.id = SM6115_MASTER_MDP_PORT0,
	.channels = 1,
	.buswidth = 16,
	.qos.qos_port = 5,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 3,
	.qos.urg_fwd_en = true,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_snoc_bimc_rt),
	.links = link_slv_snoc_bimc_rt,
};

static const u16 slv_service_snoc_links[] = {
	SM6115_SLAVE_SERVICE_SNOC,
};

static struct qcom_icc_node qhm_snoc_cfg = {
	.name = "qhm_snoc_cfg",
	.id = SM6115_MASTER_SNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(slv_service_snoc_links),
	.links = slv_service_snoc_links,
};

static const u16 mas_tic_links[] = {
	SM6115_SLAVE_APPSS,
	SM6115_SLAVE_OCIMEM,
	SM6115_SLAVE_PIMEM,
	SM6115_SLAVE_QDSS_STM,
	SM6115_SLAVE_TCU,
	SM6115_SLAVE_SNOC_BIMC,
	SM6115_SLAVE_SNOC_CNOC,
};

static struct qcom_icc_node qhm_tic = {
	.name = "qhm_tic",
	.id = SM6115_MASTER_TIC,
	.channels = 1,
	.buswidth = 4,
	.qos.qos_port = 8,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_tic_links),
	.links = mas_tic_links,
};

static struct qcom_icc_node mas_anoc_snoc = {
	.name = "mas_anoc_snoc",
	.id = SM6115_MASTER_ANOC_SNOC,
	.channels = 1,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_tic_links),
	.links = mas_tic_links,
};

static const u16 mas_bimc_snoc_links[] = {
	SM6115_SLAVE_APPSS,
	SM6115_SLAVE_SNOC_CNOC,
	SM6115_SLAVE_OCIMEM,
	SM6115_SLAVE_PIMEM,
	SM6115_SLAVE_QDSS_STM,
	SM6115_SLAVE_TCU,
};

static struct qcom_icc_node mas_bimc_snoc = {
	.name = "mas_bimc_snoc",
	.id = SM6115_MASTER_BIMC_SNOC,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = 21,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_bimc_snoc_links),
	.links = mas_bimc_snoc_links,
};

static const u16 mas_pimem_links[] = {
	SM6115_SLAVE_OCIMEM,
	SM6115_SLAVE_SNOC_BIMC,
};

static struct qcom_icc_node qxm_pimem = {
	.name = "qxm_pimem",
	.id = SM6115_MASTER_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.qos.qos_port = 20,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_pimem_links),
	.links = mas_pimem_links,
};

static struct qcom_icc_node qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = SM6115_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.qos.qos_port = 2,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_anoc_snoc),
	.links = link_slv_anoc_snoc,
};

static struct qcom_icc_node qhm_qpic = {
	.name = "qhm_qpic",
	.id = SM6115_MASTER_QPIC,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_anoc_snoc),
	.links = link_slv_anoc_snoc,
};

static struct qcom_icc_node qhm_qup0 = {
	.name = "qhm_qup0",
	.id = SM6115_MASTER_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.qos.qos_port = 0,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = 166,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_anoc_snoc),
	.links = link_slv_anoc_snoc,
};

static struct qcom_icc_node qxm_ipa = {
	.name = "qxm_ipa",
	.id = SM6115_MASTER_IPA,
	.channels = 1,
	.buswidth = 8,
	.qos.qos_port = 3,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = 59,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_anoc_snoc),
	.links = link_slv_anoc_snoc,
};

static struct qcom_icc_node xm_qdss_etr = {
	.name = "xm_qdss_etr",
	.id = SM6115_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.qos.qos_port = 12,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_anoc_snoc),
	.links = link_slv_anoc_snoc,
};

static struct qcom_icc_node xm_sdc1 = {
	.name = "xm_sdc1",
	.id = SM6115_MASTER_SDCC_1,
	.channels = 1,
	.buswidth = 8,
	.qos.qos_port = 17,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = 33,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_anoc_snoc),
	.links = link_slv_anoc_snoc,
};

static struct qcom_icc_node xm_sdc2 = {
	.name = "xm_sdc2",
	.id = SM6115_MASTER_SDCC_2,
	.channels = 1,
	.buswidth = 8,
	.qos.qos_port = 23,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = 35,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_anoc_snoc),
	.links = link_slv_anoc_snoc,
};

static struct qcom_icc_node xm_usb3_0 = {
	.name = "xm_usb3_0",
	.id = SM6115_MASTER_USB3,
	.channels = 1,
	.buswidth = 8,
	.qos.qos_port = 24,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_anoc_snoc),
	.links = link_slv_anoc_snoc,
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.id = SM6115_SLAVE_EBI_CH0,
	.channels = 2,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 0,
};

static const u16 slv_bimc_snoc_links[] = {
	SM6115_MASTER_BIMC_SNOC,
};

static struct qcom_icc_node slv_bimc_snoc = {
	.name = "slv_bimc_snoc",
	.id = SM6115_SLAVE_BIMC_SNOC,
	.channels = 1,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 2,
	.num_links = ARRAY_SIZE(slv_bimc_snoc_links),
	.links = slv_bimc_snoc_links,
};

static struct qcom_icc_node qup0_core_slave = {
	.name = "qup0_core_slave",
	.id = SM6115_SLAVE_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_ahb2phy_usb = {
	.name = "qhs_ahb2phy_usb",
	.id = SM6115_SLAVE_AHB2PHY_USB,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_apss_throttle_cfg = {
	.name = "qhs_apss_throttle_cfg",
	.id = SM6115_SLAVE_APSS_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_bimc_cfg = {
	.name = "qhs_bimc_cfg",
	.id = SM6115_SLAVE_BIMC_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_boot_rom = {
	.name = "qhs_boot_rom",
	.id = SM6115_SLAVE_BOOT_ROM,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_camera_nrt_throttle_cfg = {
	.name = "qhs_camera_nrt_throttle_cfg",
	.id = SM6115_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_camera_rt_throttle_cfg = {
	.name = "qhs_camera_rt_throttle_cfg",
	.id = SM6115_SLAVE_CAMERA_RT_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_camera_ss_cfg = {
	.name = "qhs_camera_ss_cfg",
	.id = SM6115_SLAVE_CAMERA_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SM6115_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = SM6115_SLAVE_RBCPR_CX_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_cpr_mx = {
	.name = "qhs_cpr_mx",
	.id = SM6115_SLAVE_RBCPR_MX_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = SM6115_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_dcc_cfg = {
	.name = "qhs_dcc_cfg",
	.id = SM6115_SLAVE_DCC_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_ddr_phy_cfg = {
	.name = "qhs_ddr_phy_cfg",
	.id = SM6115_SLAVE_DDR_PHY_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_ddr_ss_cfg = {
	.name = "qhs_ddr_ss_cfg",
	.id = SM6115_SLAVE_DDR_SS_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_disp_ss_cfg = {
	.name = "qhs_disp_ss_cfg",
	.id = SM6115_SLAVE_DISPLAY_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_display_throttle_cfg = {
	.name = "qhs_display_throttle_cfg",
	.id = SM6115_SLAVE_DISPLAY_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_gpu_cfg = {
	.name = "qhs_gpu_cfg",
	.id = SM6115_SLAVE_GPU_CFG,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_gpu_throttle_cfg = {
	.name = "qhs_gpu_throttle_cfg",
	.id = SM6115_SLAVE_GPU_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_hwkm = {
	.name = "qhs_hwkm",
	.id = SM6115_SLAVE_HWKM_CORE,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SM6115_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_ipa_cfg = {
	.name = "qhs_ipa_cfg",
	.id = SM6115_SLAVE_IPA_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_lpass = {
	.name = "qhs_lpass",
	.id = SM6115_SLAVE_LPASS,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_mapss = {
	.name = "qhs_mapss",
	.id = SM6115_SLAVE_MAPSS,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_mdsp_mpu_cfg = {
	.name = "qhs_mdsp_mpu_cfg",
	.id = SM6115_SLAVE_MDSP_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_mesg_ram = {
	.name = "qhs_mesg_ram",
	.id = SM6115_SLAVE_MESSAGE_RAM,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_mss = {
	.name = "qhs_mss",
	.id = SM6115_SLAVE_CNOC_MSS,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_pdm = {
	.name = "qhs_pdm",
	.id = SM6115_SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = SM6115_SLAVE_PIMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_pka_wrapper = {
	.name = "qhs_pka_wrapper",
	.id = SM6115_SLAVE_PKA_CORE,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_pmic_arb = {
	.name = "qhs_pmic_arb",
	.id = SM6115_SLAVE_PMIC_ARB,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SM6115_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_qm_cfg = {
	.name = "qhs_qm_cfg",
	.id = SM6115_SLAVE_QM_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_qm_mpu_cfg = {
	.name = "qhs_qm_mpu_cfg",
	.id = SM6115_SLAVE_QM_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_qpic = {
	.name = "qhs_qpic",
	.id = SM6115_SLAVE_QPIC,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_qup0 = {
	.name = "qhs_qup0",
	.id = SM6115_SLAVE_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_rpm = {
	.name = "qhs_rpm",
	.id = SM6115_SLAVE_RPM,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_sdc1 = {
	.name = "qhs_sdc1",
	.id = SM6115_SLAVE_SDCC_1,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = SM6115_SLAVE_SDCC_2,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_security = {
	.name = "qhs_security",
	.id = SM6115_SLAVE_SECURITY,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static const u16 slv_snoc_cfg_links[] = {
	SM6115_MASTER_SNOC_CFG,
};

static struct qcom_icc_node qhs_snoc_cfg = {
	.name = "qhs_snoc_cfg",
	.id = SM6115_SLAVE_SNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(slv_snoc_cfg_links),
	.links = slv_snoc_cfg_links,
};

static struct qcom_icc_node qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SM6115_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_tlmm = {
	.name = "qhs_tlmm",
	.id = SM6115_SLAVE_TLMM,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_usb3 = {
	.name = "qhs_usb3",
	.id = SM6115_SLAVE_USB3,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = SM6115_SLAVE_VENUS_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_venus_throttle_cfg = {
	.name = "qhs_venus_throttle_cfg",
	.id = SM6115_SLAVE_VENUS_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.id = SM6115_SLAVE_VSENSE_CTRL_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node srvc_cnoc = {
	.name = "srvc_cnoc",
	.id = SM6115_SLAVE_SERVICE_CNOC,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static const u16 slv_snoc_bimc_nrt_links[] = {
	SM6115_MASTER_SNOC_BIMC_NRT,
};

static struct qcom_icc_node slv_snoc_bimc_nrt = {
	.name = "slv_snoc_bimc_nrt",
	.id = SM6115_SLAVE_SNOC_BIMC_NRT,
	.channels = 1,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(slv_snoc_bimc_nrt_links),
	.links = slv_snoc_bimc_nrt_links,
};

static const u16 slv_snoc_bimc_rt_links[] = {
	SM6115_MASTER_SNOC_BIMC_RT,
};

static struct qcom_icc_node slv_snoc_bimc_rt = {
	.name = "slv_snoc_bimc_rt",
	.id = SM6115_SLAVE_SNOC_BIMC_RT,
	.channels = 1,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(slv_snoc_bimc_rt_links),
	.links = slv_snoc_bimc_rt_links,
};

static struct qcom_icc_node qhs_apss = {
	.name = "qhs_apss",
	.id = SM6115_SLAVE_APPSS,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static const u16 slv_snoc_cnoc_links[] = {
	SM6115_MASTER_SNOC_CNOC
};

static struct qcom_icc_node slv_snoc_cnoc = {
	.name = "slv_snoc_cnoc",
	.id = SM6115_SLAVE_SNOC_CNOC,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 25,
	.num_links = ARRAY_SIZE(slv_snoc_cnoc_links),
	.links = slv_snoc_cnoc_links,
};

static struct qcom_icc_node qxs_imem = {
	.name = "qxs_imem",
	.id = SM6115_SLAVE_OCIMEM,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 26,
};

static struct qcom_icc_node qxs_pimem = {
	.name = "qxs_pimem",
	.id = SM6115_SLAVE_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static const u16 slv_snoc_bimc_links[] = {
	SM6115_MASTER_SNOC_BIMC,
};

static struct qcom_icc_node slv_snoc_bimc = {
	.name = "slv_snoc_bimc",
	.id = SM6115_SLAVE_SNOC_BIMC,
	.channels = 1,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 24,
	.num_links = ARRAY_SIZE(slv_snoc_bimc_links),
	.links = slv_snoc_bimc_links,
};

static struct qcom_icc_node srvc_snoc = {
	.name = "srvc_snoc",
	.id = SM6115_SLAVE_SERVICE_SNOC,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SM6115_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 30,
};

static struct qcom_icc_node xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SM6115_SLAVE_TCU,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static const u16 slv_anoc_snoc_links[] = {
	SM6115_MASTER_ANOC_SNOC,
};

static struct qcom_icc_node slv_anoc_snoc = {
	.name = "slv_anoc_snoc",
	.id = SM6115_SLAVE_ANOC_SNOC,
	.channels = 1,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(slv_anoc_snoc_links),
	.links = slv_anoc_snoc_links,
};

static struct qcom_icc_node * const bimc_nodes[] = {
	[MASTER_AMPSS_M0] = &apps_proc,
	[MASTER_SNOC_BIMC_RT] = &mas_snoc_bimc_rt,
	[MASTER_SNOC_BIMC_NRT] = &mas_snoc_bimc_nrt,
	[SNOC_BIMC_MAS] = &mas_snoc_bimc,
	[MASTER_GRAPHICS_3D] = &qnm_gpu,
	[MASTER_TCU_0] = &tcu_0,
	[SLAVE_EBI_CH0] = &ebi,
	[BIMC_SNOC_SLV] = &slv_bimc_snoc,
};

static const struct regmap_config bimc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x80000,
	.fast_io	= true,
};

static const struct qcom_icc_desc sm6115_bimc = {
	.type = QCOM_ICC_BIMC,
	.nodes = bimc_nodes,
	.num_nodes = ARRAY_SIZE(bimc_nodes),
	.regmap_cfg = &bimc_regmap_config,
	.bus_clk_desc = &bimc_clk,
	.keep_alive = true,
	.qos_offset = 0x8000,
	.ab_coeff = 153,
};

static struct qcom_icc_node * const config_noc_nodes[] = {
	[SNOC_CNOC_MAS] = &mas_snoc_cnoc,
	[MASTER_QDSS_DAP] = &xm_dap,
	[SLAVE_AHB2PHY_USB] = &qhs_ahb2phy_usb,
	[SLAVE_APSS_THROTTLE_CFG] = &qhs_apss_throttle_cfg,
	[SLAVE_BIMC_CFG] = &qhs_bimc_cfg,
	[SLAVE_BOOT_ROM] = &qhs_boot_rom,
	[SLAVE_CAMERA_NRT_THROTTLE_CFG] = &qhs_camera_nrt_throttle_cfg,
	[SLAVE_CAMERA_RT_THROTTLE_CFG] = &qhs_camera_rt_throttle_cfg,
	[SLAVE_CAMERA_CFG] = &qhs_camera_ss_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MX_CFG] = &qhs_cpr_mx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_DCC_CFG] = &qhs_dcc_cfg,
	[SLAVE_DDR_PHY_CFG] = &qhs_ddr_phy_cfg,
	[SLAVE_DDR_SS_CFG] = &qhs_ddr_ss_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_disp_ss_cfg,
	[SLAVE_DISPLAY_THROTTLE_CFG] = &qhs_display_throttle_cfg,
	[SLAVE_GPU_CFG] = &qhs_gpu_cfg,
	[SLAVE_GPU_THROTTLE_CFG] = &qhs_gpu_throttle_cfg,
	[SLAVE_HWKM_CORE] = &qhs_hwkm,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa_cfg,
	[SLAVE_LPASS] = &qhs_lpass,
	[SLAVE_MAPSS] = &qhs_mapss,
	[SLAVE_MDSP_MPU_CFG] = &qhs_mdsp_mpu_cfg,
	[SLAVE_MESSAGE_RAM] = &qhs_mesg_ram,
	[SLAVE_CNOC_MSS] = &qhs_mss,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PKA_CORE] = &qhs_pka_wrapper,
	[SLAVE_PMIC_ARB] = &qhs_pmic_arb,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QM_CFG] = &qhs_qm_cfg,
	[SLAVE_QM_MPU_CFG] = &qhs_qm_mpu_cfg,
	[SLAVE_QPIC] = &qhs_qpic,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_RPM] = &qhs_rpm,
	[SLAVE_SDCC_1] = &qhs_sdc1,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SECURITY] = &qhs_security,
	[SLAVE_SNOC_CFG] = &qhs_snoc_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_USB3] = &qhs_usb3,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VENUS_THROTTLE_CFG] = &qhs_venus_throttle_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_SERVICE_CNOC] = &srvc_cnoc,
};

static const struct regmap_config cnoc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x6200,
	.fast_io	= true,
};

static const struct qcom_icc_desc sm6115_config_noc = {
	.type = QCOM_ICC_QNOC,
	.nodes = config_noc_nodes,
	.num_nodes = ARRAY_SIZE(config_noc_nodes),
	.regmap_cfg = &cnoc_regmap_config,
	.intf_clocks = cnoc_intf_clocks,
	.num_intf_clocks = ARRAY_SIZE(cnoc_intf_clocks),
	.bus_clk_desc = &bus_1_clk,
	.keep_alive = true,
};

static struct qcom_icc_node * const sys_noc_nodes[] = {
	[MASTER_CRYPTO_CORE0] = &crypto_c0,
	[MASTER_SNOC_CFG] = &qhm_snoc_cfg,
	[MASTER_TIC] = &qhm_tic,
	[MASTER_ANOC_SNOC] = &mas_anoc_snoc,
	[BIMC_SNOC_MAS] = &mas_bimc_snoc,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QPIC] = &qhm_qpic,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[MASTER_SDCC_1] = &xm_sdc1,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_USB3] = &xm_usb3_0,
	[SLAVE_APPSS] = &qhs_apss,
	[SNOC_CNOC_SLV] = &slv_snoc_cnoc,
	[SLAVE_OCIMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SNOC_BIMC_SLV] = &slv_snoc_bimc,
	[SLAVE_SERVICE_SNOC] = &srvc_snoc,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
	[SLAVE_ANOC_SNOC] = &slv_anoc_snoc,
};

static const struct regmap_config sys_noc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x5f080,
	.fast_io	= true,
};

static const struct qcom_icc_desc sm6115_sys_noc = {
	.type = QCOM_ICC_QNOC,
	.nodes = sys_noc_nodes,
	.num_nodes = ARRAY_SIZE(sys_noc_nodes),
	.regmap_cfg = &sys_noc_regmap_config,
	.intf_clocks = snoc_intf_clocks,
	.num_intf_clocks = ARRAY_SIZE(snoc_intf_clocks),
	.bus_clk_desc = &bus_2_clk,
	.qos_offset = 0x15000,
	.keep_alive = true,
};

static struct qcom_icc_node * const clk_virt_nodes[] = {
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
};

static const struct qcom_icc_desc sm6115_clk_virt = {
	.type = QCOM_ICC_QNOC,
	.nodes = clk_virt_nodes,
	.num_nodes = ARRAY_SIZE(clk_virt_nodes),
	.regmap_cfg = &sys_noc_regmap_config,
	.bus_clk_desc = &qup_clk,
	.keep_alive = true,
};

static struct qcom_icc_node * const mmnrt_virt_nodes[] = {
	[MASTER_CAMNOC_SF] = &qnm_camera_nrt,
	[MASTER_VIDEO_P0] = &qxm_venus0,
	[MASTER_VIDEO_PROC] = &qxm_venus_cpu,
	[SLAVE_SNOC_BIMC_NRT] = &slv_snoc_bimc_nrt,
};

static const struct qcom_icc_desc sm6115_mmnrt_virt = {
	.type = QCOM_ICC_QNOC,
	.nodes = mmnrt_virt_nodes,
	.num_nodes = ARRAY_SIZE(mmnrt_virt_nodes),
	.regmap_cfg = &sys_noc_regmap_config,
	.bus_clk_desc = &mmaxi_0_clk,
	.keep_alive = true,
	.qos_offset = 0x15000,
	.ab_coeff = 142,
};

static struct qcom_icc_node * const mmrt_virt_nodes[] = {
	[MASTER_CAMNOC_HF] = &qnm_camera_rt,
	[MASTER_MDP_PORT0] = &qxm_mdp0,
	[SLAVE_SNOC_BIMC_RT] = &slv_snoc_bimc_rt,
};

static const struct qcom_icc_desc sm6115_mmrt_virt = {
	.type = QCOM_ICC_QNOC,
	.nodes = mmrt_virt_nodes,
	.num_nodes = ARRAY_SIZE(mmrt_virt_nodes),
	.regmap_cfg = &sys_noc_regmap_config,
	.bus_clk_desc = &mmaxi_1_clk,
	.keep_alive = true,
	.qos_offset = 0x15000,
	.ab_coeff = 139,
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,sm6115-bimc", .data = &sm6115_bimc },
	{ .compatible = "qcom,sm6115-clk-virt", .data = &sm6115_clk_virt },
	{ .compatible = "qcom,sm6115-cnoc", .data = &sm6115_config_noc },
	{ .compatible = "qcom,sm6115-mmrt-virt", .data = &sm6115_mmrt_virt },
	{ .compatible = "qcom,sm6115-mmnrt-virt", .data = &sm6115_mmnrt_virt },
	{ .compatible = "qcom,sm6115-snoc", .data = &sm6115_sys_noc },
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qnoc_probe,
	.remove_new = qnoc_remove,
	.driver = {
		.name = "qnoc-sm6115",
		.of_match_table = qnoc_of_match,
		.sync_state = icc_sync_state,
	},
};

static int __init qnoc_driver_init(void)
{
	return platform_driver_register(&qnoc_driver);
}
core_initcall(qnoc_driver_init);

static void __exit qnoc_driver_exit(void)
{
	platform_driver_unregister(&qnoc_driver);
}
module_exit(qnoc_driver_exit);

MODULE_DESCRIPTION("SM6115 NoC driver");
MODULE_LICENSE("GPL");
