// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Inanalvation Center, Inc. All rights reserved.
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

static const char * const sanalc_intf_clocks[] = {
	"cpu_axi",
	"ufs_axi",
	"usb_axi",
	"ipa", /* Required by qxm_ipa */
};

static const char * const canalc_intf_clocks[] = {
	"usb_axi",
};

enum {
	SM6115_MASTER_AMPSS_M0,
	SM6115_MASTER_AANALC_SANALC,
	SM6115_MASTER_BIMC_SANALC,
	SM6115_MASTER_CAMANALC_HF,
	SM6115_MASTER_CAMANALC_SF,
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
	SM6115_MASTER_SANALC_BIMC_NRT,
	SM6115_MASTER_SANALC_BIMC_RT,
	SM6115_MASTER_SANALC_BIMC,
	SM6115_MASTER_SANALC_CFG,
	SM6115_MASTER_SANALC_CANALC,
	SM6115_MASTER_TCU_0,
	SM6115_MASTER_TIC,
	SM6115_MASTER_USB3,
	SM6115_MASTER_VIDEO_P0,
	SM6115_MASTER_VIDEO_PROC,

	SM6115_SLAVE_AHB2PHY_USB,
	SM6115_SLAVE_AANALC_SANALC,
	SM6115_SLAVE_APPSS,
	SM6115_SLAVE_APSS_THROTTLE_CFG,
	SM6115_SLAVE_BIMC_CFG,
	SM6115_SLAVE_BIMC_SANALC,
	SM6115_SLAVE_BOOT_ROM,
	SM6115_SLAVE_CAMERA_CFG,
	SM6115_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	SM6115_SLAVE_CAMERA_RT_THROTTLE_CFG,
	SM6115_SLAVE_CLK_CTL,
	SM6115_SLAVE_CANALC_MSS,
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
	SM6115_SLAVE_SERVICE_CANALC,
	SM6115_SLAVE_SERVICE_SANALC,
	SM6115_SLAVE_SANALC_BIMC_NRT,
	SM6115_SLAVE_SANALC_BIMC_RT,
	SM6115_SLAVE_SANALC_BIMC,
	SM6115_SLAVE_SANALC_CFG,
	SM6115_SLAVE_SANALC_CANALC,
	SM6115_SLAVE_TCSR,
	SM6115_SLAVE_TCU,
	SM6115_SLAVE_TLMM,
	SM6115_SLAVE_USB3,
	SM6115_SLAVE_VENUS_CFG,
	SM6115_SLAVE_VENUS_THROTTLE_CFG,
	SM6115_SLAVE_VSENSE_CTRL_CFG,
};

static const u16 slv_ebi_slv_bimc_sanalc_links[] = {
	SM6115_SLAVE_EBI_CH0,
	SM6115_SLAVE_BIMC_SANALC,
};

static struct qcom_icc_analde apps_proc = {
	.name = "apps_proc",
	.id = SM6115_MASTER_AMPSS_M0,
	.channels = 1,
	.buswidth = 16,
	.qos.qos_port = 0,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.prio_level = 0,
	.qos.areq_prio = 0,
	.mas_rpm_id = 0,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(slv_ebi_slv_bimc_sanalc_links),
	.links = slv_ebi_slv_bimc_sanalc_links,
};

static const u16 link_slv_ebi[] = {
	SM6115_SLAVE_EBI_CH0,
};

static struct qcom_icc_analde mas_sanalc_bimc_rt = {
	.name = "mas_sanalc_bimc_rt",
	.id = SM6115_MASTER_SANALC_BIMC_RT,
	.channels = 1,
	.buswidth = 16,
	.qos.qos_port = 2,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_ebi),
	.links = link_slv_ebi,
};

static struct qcom_icc_analde mas_sanalc_bimc_nrt = {
	.name = "mas_sanalc_bimc_nrt",
	.id = SM6115_MASTER_SANALC_BIMC_NRT,
	.channels = 1,
	.buswidth = 16,
	.qos.qos_port = 3,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_ebi),
	.links = link_slv_ebi,
};

static struct qcom_icc_analde mas_sanalc_bimc = {
	.name = "mas_sanalc_bimc",
	.id = SM6115_MASTER_SANALC_BIMC,
	.channels = 1,
	.buswidth = 16,
	.qos.qos_port = 6,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.mas_rpm_id = 3,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_ebi),
	.links = link_slv_ebi,
};

static struct qcom_icc_analde qnm_gpu = {
	.name = "qnm_gpu",
	.id = SM6115_MASTER_GRAPHICS_3D,
	.channels = 1,
	.buswidth = 32,
	.qos.qos_port = 1,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.prio_level = 0,
	.qos.areq_prio = 0,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(slv_ebi_slv_bimc_sanalc_links),
	.links = slv_ebi_slv_bimc_sanalc_links,
};

static struct qcom_icc_analde tcu_0 = {
	.name = "tcu_0",
	.id = SM6115_MASTER_TCU_0,
	.channels = 1,
	.buswidth = 8,
	.qos.qos_port = 4,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.prio_level = 6,
	.qos.areq_prio = 6,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(slv_ebi_slv_bimc_sanalc_links),
	.links = slv_ebi_slv_bimc_sanalc_links,
};

static const u16 qup_core_0_links[] = {
	SM6115_SLAVE_QUP_CORE_0,
};

static struct qcom_icc_analde qup0_core_master = {
	.name = "qup0_core_master",
	.id = SM6115_MASTER_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = 170,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qup_core_0_links),
	.links = qup_core_0_links,
};

static const u16 link_slv_aanalc_sanalc[] = {
	SM6115_SLAVE_AANALC_SANALC,
};

static struct qcom_icc_analde crypto_c0 = {
	.name = "crypto_c0",
	.id = SM6115_MASTER_CRYPTO_CORE0,
	.channels = 1,
	.buswidth = 8,
	.qos.qos_port = 43,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = 23,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_aanalc_sanalc),
	.links = link_slv_aanalc_sanalc,
};

static const u16 mas_sanalc_canalc_links[] = {
	SM6115_SLAVE_AHB2PHY_USB,
	SM6115_SLAVE_APSS_THROTTLE_CFG,
	SM6115_SLAVE_BIMC_CFG,
	SM6115_SLAVE_BOOT_ROM,
	SM6115_SLAVE_CAMERA_CFG,
	SM6115_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	SM6115_SLAVE_CAMERA_RT_THROTTLE_CFG,
	SM6115_SLAVE_CLK_CTL,
	SM6115_SLAVE_CANALC_MSS,
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
	SM6115_SLAVE_SERVICE_CANALC,
	SM6115_SLAVE_SANALC_CFG,
	SM6115_SLAVE_TCSR,
	SM6115_SLAVE_TLMM,
	SM6115_SLAVE_USB3,
	SM6115_SLAVE_VENUS_CFG,
	SM6115_SLAVE_VENUS_THROTTLE_CFG,
	SM6115_SLAVE_VSENSE_CTRL_CFG,
};

static struct qcom_icc_analde mas_sanalc_canalc = {
	.name = "mas_sanalc_canalc",
	.id = SM6115_MASTER_SANALC_CANALC,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_sanalc_canalc_links),
	.links = mas_sanalc_canalc_links,
};

static struct qcom_icc_analde xm_dap = {
	.name = "xm_dap",
	.id = SM6115_MASTER_QDSS_DAP,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_sanalc_canalc_links),
	.links = mas_sanalc_canalc_links,
};

static const u16 link_slv_sanalc_bimc_nrt[] = {
	SM6115_SLAVE_SANALC_BIMC_NRT,
};

static struct qcom_icc_analde qnm_camera_nrt = {
	.name = "qnm_camera_nrt",
	.id = SM6115_MASTER_CAMANALC_SF,
	.channels = 1,
	.buswidth = 32,
	.qos.qos_port = 25,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 3,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_sanalc_bimc_nrt),
	.links = link_slv_sanalc_bimc_nrt,
};

static struct qcom_icc_analde qxm_venus0 = {
	.name = "qxm_venus0",
	.id = SM6115_MASTER_VIDEO_P0,
	.channels = 1,
	.buswidth = 16,
	.qos.qos_port = 30,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 3,
	.qos.urg_fwd_en = true,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_sanalc_bimc_nrt),
	.links = link_slv_sanalc_bimc_nrt,
};

static struct qcom_icc_analde qxm_venus_cpu = {
	.name = "qxm_venus_cpu",
	.id = SM6115_MASTER_VIDEO_PROC,
	.channels = 1,
	.buswidth = 8,
	.qos.qos_port = 34,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_sanalc_bimc_nrt),
	.links = link_slv_sanalc_bimc_nrt,
};

static const u16 link_slv_sanalc_bimc_rt[] = {
	SM6115_SLAVE_SANALC_BIMC_RT,
};

static struct qcom_icc_analde qnm_camera_rt = {
	.name = "qnm_camera_rt",
	.id = SM6115_MASTER_CAMANALC_HF,
	.channels = 1,
	.buswidth = 32,
	.qos.qos_port = 31,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 3,
	.qos.urg_fwd_en = true,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_sanalc_bimc_rt),
	.links = link_slv_sanalc_bimc_rt,
};

static struct qcom_icc_analde qxm_mdp0 = {
	.name = "qxm_mdp0",
	.id = SM6115_MASTER_MDP_PORT0,
	.channels = 1,
	.buswidth = 16,
	.qos.qos_port = 26,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 3,
	.qos.urg_fwd_en = true,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_sanalc_bimc_rt),
	.links = link_slv_sanalc_bimc_rt,
};

static const u16 slv_service_sanalc_links[] = {
	SM6115_SLAVE_SERVICE_SANALC,
};

static struct qcom_icc_analde qhm_sanalc_cfg = {
	.name = "qhm_sanalc_cfg",
	.id = SM6115_MASTER_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(slv_service_sanalc_links),
	.links = slv_service_sanalc_links,
};

static const u16 mas_tic_links[] = {
	SM6115_SLAVE_APPSS,
	SM6115_SLAVE_OCIMEM,
	SM6115_SLAVE_PIMEM,
	SM6115_SLAVE_QDSS_STM,
	SM6115_SLAVE_TCU,
	SM6115_SLAVE_SANALC_BIMC,
	SM6115_SLAVE_SANALC_CANALC,
};

static struct qcom_icc_analde qhm_tic = {
	.name = "qhm_tic",
	.id = SM6115_MASTER_TIC,
	.channels = 1,
	.buswidth = 4,
	.qos.qos_port = 29,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_tic_links),
	.links = mas_tic_links,
};

static struct qcom_icc_analde mas_aanalc_sanalc = {
	.name = "mas_aanalc_sanalc",
	.id = SM6115_MASTER_AANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_tic_links),
	.links = mas_tic_links,
};

static const u16 mas_bimc_sanalc_links[] = {
	SM6115_SLAVE_APPSS,
	SM6115_SLAVE_SANALC_CANALC,
	SM6115_SLAVE_OCIMEM,
	SM6115_SLAVE_PIMEM,
	SM6115_SLAVE_QDSS_STM,
	SM6115_SLAVE_TCU,
};

static struct qcom_icc_analde mas_bimc_sanalc = {
	.name = "mas_bimc_sanalc",
	.id = SM6115_MASTER_BIMC_SANALC,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = 21,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_bimc_sanalc_links),
	.links = mas_bimc_sanalc_links,
};

static const u16 mas_pimem_links[] = {
	SM6115_SLAVE_OCIMEM,
	SM6115_SLAVE_SANALC_BIMC,
};

static struct qcom_icc_analde qxm_pimem = {
	.name = "qxm_pimem",
	.id = SM6115_MASTER_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.qos.qos_port = 41,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_pimem_links),
	.links = mas_pimem_links,
};

static struct qcom_icc_analde qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = SM6115_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.qos.qos_port = 23,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_aanalc_sanalc),
	.links = link_slv_aanalc_sanalc,
};

static struct qcom_icc_analde qhm_qpic = {
	.name = "qhm_qpic",
	.id = SM6115_MASTER_QPIC,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_aanalc_sanalc),
	.links = link_slv_aanalc_sanalc,
};

static struct qcom_icc_analde qhm_qup0 = {
	.name = "qhm_qup0",
	.id = SM6115_MASTER_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.qos.qos_port = 21,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = 166,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_aanalc_sanalc),
	.links = link_slv_aanalc_sanalc,
};

static struct qcom_icc_analde qxm_ipa = {
	.name = "qxm_ipa",
	.id = SM6115_MASTER_IPA,
	.channels = 1,
	.buswidth = 8,
	.qos.qos_port = 24,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = 59,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_aanalc_sanalc),
	.links = link_slv_aanalc_sanalc,
};

static struct qcom_icc_analde xm_qdss_etr = {
	.name = "xm_qdss_etr",
	.id = SM6115_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.qos.qos_port = 33,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_aanalc_sanalc),
	.links = link_slv_aanalc_sanalc,
};

static struct qcom_icc_analde xm_sdc1 = {
	.name = "xm_sdc1",
	.id = SM6115_MASTER_SDCC_1,
	.channels = 1,
	.buswidth = 8,
	.qos.qos_port = 38,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = 33,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_aanalc_sanalc),
	.links = link_slv_aanalc_sanalc,
};

static struct qcom_icc_analde xm_sdc2 = {
	.name = "xm_sdc2",
	.id = SM6115_MASTER_SDCC_2,
	.channels = 1,
	.buswidth = 8,
	.qos.qos_port = 44,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = 35,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_aanalc_sanalc),
	.links = link_slv_aanalc_sanalc,
};

static struct qcom_icc_analde xm_usb3_0 = {
	.name = "xm_usb3_0",
	.id = SM6115_MASTER_USB3,
	.channels = 1,
	.buswidth = 8,
	.qos.qos_port = 45,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(link_slv_aanalc_sanalc),
	.links = link_slv_aanalc_sanalc,
};

static struct qcom_icc_analde ebi = {
	.name = "ebi",
	.id = SM6115_SLAVE_EBI_CH0,
	.channels = 2,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 0,
};

static const u16 slv_bimc_sanalc_links[] = {
	SM6115_MASTER_BIMC_SANALC,
};

static struct qcom_icc_analde slv_bimc_sanalc = {
	.name = "slv_bimc_sanalc",
	.id = SM6115_SLAVE_BIMC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 2,
	.num_links = ARRAY_SIZE(slv_bimc_sanalc_links),
	.links = slv_bimc_sanalc_links,
};

static struct qcom_icc_analde qup0_core_slave = {
	.name = "qup0_core_slave",
	.id = SM6115_SLAVE_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_ahb2phy_usb = {
	.name = "qhs_ahb2phy_usb",
	.id = SM6115_SLAVE_AHB2PHY_USB,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_apss_throttle_cfg = {
	.name = "qhs_apss_throttle_cfg",
	.id = SM6115_SLAVE_APSS_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_bimc_cfg = {
	.name = "qhs_bimc_cfg",
	.id = SM6115_SLAVE_BIMC_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_boot_rom = {
	.name = "qhs_boot_rom",
	.id = SM6115_SLAVE_BOOT_ROM,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_camera_nrt_throttle_cfg = {
	.name = "qhs_camera_nrt_throttle_cfg",
	.id = SM6115_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_camera_rt_throttle_cfg = {
	.name = "qhs_camera_rt_throttle_cfg",
	.id = SM6115_SLAVE_CAMERA_RT_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_camera_ss_cfg = {
	.name = "qhs_camera_ss_cfg",
	.id = SM6115_SLAVE_CAMERA_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SM6115_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = SM6115_SLAVE_RBCPR_CX_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_cpr_mx = {
	.name = "qhs_cpr_mx",
	.id = SM6115_SLAVE_RBCPR_MX_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = SM6115_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_dcc_cfg = {
	.name = "qhs_dcc_cfg",
	.id = SM6115_SLAVE_DCC_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_ddr_phy_cfg = {
	.name = "qhs_ddr_phy_cfg",
	.id = SM6115_SLAVE_DDR_PHY_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_ddr_ss_cfg = {
	.name = "qhs_ddr_ss_cfg",
	.id = SM6115_SLAVE_DDR_SS_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_disp_ss_cfg = {
	.name = "qhs_disp_ss_cfg",
	.id = SM6115_SLAVE_DISPLAY_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_display_throttle_cfg = {
	.name = "qhs_display_throttle_cfg",
	.id = SM6115_SLAVE_DISPLAY_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_gpu_cfg = {
	.name = "qhs_gpu_cfg",
	.id = SM6115_SLAVE_GPU_CFG,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_gpu_throttle_cfg = {
	.name = "qhs_gpu_throttle_cfg",
	.id = SM6115_SLAVE_GPU_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_hwkm = {
	.name = "qhs_hwkm",
	.id = SM6115_SLAVE_HWKM_CORE,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SM6115_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_ipa_cfg = {
	.name = "qhs_ipa_cfg",
	.id = SM6115_SLAVE_IPA_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_lpass = {
	.name = "qhs_lpass",
	.id = SM6115_SLAVE_LPASS,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_mapss = {
	.name = "qhs_mapss",
	.id = SM6115_SLAVE_MAPSS,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_mdsp_mpu_cfg = {
	.name = "qhs_mdsp_mpu_cfg",
	.id = SM6115_SLAVE_MDSP_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_mesg_ram = {
	.name = "qhs_mesg_ram",
	.id = SM6115_SLAVE_MESSAGE_RAM,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_mss = {
	.name = "qhs_mss",
	.id = SM6115_SLAVE_CANALC_MSS,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_pdm = {
	.name = "qhs_pdm",
	.id = SM6115_SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = SM6115_SLAVE_PIMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_pka_wrapper = {
	.name = "qhs_pka_wrapper",
	.id = SM6115_SLAVE_PKA_CORE,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_pmic_arb = {
	.name = "qhs_pmic_arb",
	.id = SM6115_SLAVE_PMIC_ARB,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SM6115_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_qm_cfg = {
	.name = "qhs_qm_cfg",
	.id = SM6115_SLAVE_QM_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_qm_mpu_cfg = {
	.name = "qhs_qm_mpu_cfg",
	.id = SM6115_SLAVE_QM_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_qpic = {
	.name = "qhs_qpic",
	.id = SM6115_SLAVE_QPIC,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_qup0 = {
	.name = "qhs_qup0",
	.id = SM6115_SLAVE_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_rpm = {
	.name = "qhs_rpm",
	.id = SM6115_SLAVE_RPM,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_sdc1 = {
	.name = "qhs_sdc1",
	.id = SM6115_SLAVE_SDCC_1,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = SM6115_SLAVE_SDCC_2,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_security = {
	.name = "qhs_security",
	.id = SM6115_SLAVE_SECURITY,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static const u16 slv_sanalc_cfg_links[] = {
	SM6115_MASTER_SANALC_CFG,
};

static struct qcom_icc_analde qhs_sanalc_cfg = {
	.name = "qhs_sanalc_cfg",
	.id = SM6115_SLAVE_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(slv_sanalc_cfg_links),
	.links = slv_sanalc_cfg_links,
};

static struct qcom_icc_analde qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SM6115_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_tlmm = {
	.name = "qhs_tlmm",
	.id = SM6115_SLAVE_TLMM,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_usb3 = {
	.name = "qhs_usb3",
	.id = SM6115_SLAVE_USB3,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = SM6115_SLAVE_VENUS_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_venus_throttle_cfg = {
	.name = "qhs_venus_throttle_cfg",
	.id = SM6115_SLAVE_VENUS_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.id = SM6115_SLAVE_VSENSE_CTRL_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde srvc_canalc = {
	.name = "srvc_canalc",
	.id = SM6115_SLAVE_SERVICE_CANALC,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static const u16 slv_sanalc_bimc_nrt_links[] = {
	SM6115_MASTER_SANALC_BIMC_NRT,
};

static struct qcom_icc_analde slv_sanalc_bimc_nrt = {
	.name = "slv_sanalc_bimc_nrt",
	.id = SM6115_SLAVE_SANALC_BIMC_NRT,
	.channels = 1,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(slv_sanalc_bimc_nrt_links),
	.links = slv_sanalc_bimc_nrt_links,
};

static const u16 slv_sanalc_bimc_rt_links[] = {
	SM6115_MASTER_SANALC_BIMC_RT,
};

static struct qcom_icc_analde slv_sanalc_bimc_rt = {
	.name = "slv_sanalc_bimc_rt",
	.id = SM6115_SLAVE_SANALC_BIMC_RT,
	.channels = 1,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(slv_sanalc_bimc_rt_links),
	.links = slv_sanalc_bimc_rt_links,
};

static struct qcom_icc_analde qhs_apss = {
	.name = "qhs_apss",
	.id = SM6115_SLAVE_APPSS,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static const u16 slv_sanalc_canalc_links[] = {
	SM6115_MASTER_SANALC_CANALC
};

static struct qcom_icc_analde slv_sanalc_canalc = {
	.name = "slv_sanalc_canalc",
	.id = SM6115_SLAVE_SANALC_CANALC,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 25,
	.num_links = ARRAY_SIZE(slv_sanalc_canalc_links),
	.links = slv_sanalc_canalc_links,
};

static struct qcom_icc_analde qxs_imem = {
	.name = "qxs_imem",
	.id = SM6115_SLAVE_OCIMEM,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 26,
};

static struct qcom_icc_analde qxs_pimem = {
	.name = "qxs_pimem",
	.id = SM6115_SLAVE_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static const u16 slv_sanalc_bimc_links[] = {
	SM6115_MASTER_SANALC_BIMC,
};

static struct qcom_icc_analde slv_sanalc_bimc = {
	.name = "slv_sanalc_bimc",
	.id = SM6115_SLAVE_SANALC_BIMC,
	.channels = 1,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 24,
	.num_links = ARRAY_SIZE(slv_sanalc_bimc_links),
	.links = slv_sanalc_bimc_links,
};

static struct qcom_icc_analde srvc_sanalc = {
	.name = "srvc_sanalc",
	.id = SM6115_SLAVE_SERVICE_SANALC,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SM6115_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 30,
};

static struct qcom_icc_analde xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SM6115_SLAVE_TCU,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static const u16 slv_aanalc_sanalc_links[] = {
	SM6115_MASTER_AANALC_SANALC,
};

static struct qcom_icc_analde slv_aanalc_sanalc = {
	.name = "slv_aanalc_sanalc",
	.id = SM6115_SLAVE_AANALC_SANALC,
	.channels = 1,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(slv_aanalc_sanalc_links),
	.links = slv_aanalc_sanalc_links,
};

static struct qcom_icc_analde *bimc_analdes[] = {
	[MASTER_AMPSS_M0] = &apps_proc,
	[MASTER_SANALC_BIMC_RT] = &mas_sanalc_bimc_rt,
	[MASTER_SANALC_BIMC_NRT] = &mas_sanalc_bimc_nrt,
	[SANALC_BIMC_MAS] = &mas_sanalc_bimc,
	[MASTER_GRAPHICS_3D] = &qnm_gpu,
	[MASTER_TCU_0] = &tcu_0,
	[SLAVE_EBI_CH0] = &ebi,
	[BIMC_SANALC_SLV] = &slv_bimc_sanalc,
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
	.analdes = bimc_analdes,
	.num_analdes = ARRAY_SIZE(bimc_analdes),
	.regmap_cfg = &bimc_regmap_config,
	.bus_clk_desc = &bimc_clk,
	.keep_alive = true,
	.qos_offset = 0x8000,
	.ab_coeff = 153,
};

static struct qcom_icc_analde *config_analc_analdes[] = {
	[SANALC_CANALC_MAS] = &mas_sanalc_canalc,
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
	[SLAVE_CANALC_MSS] = &qhs_mss,
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
	[SLAVE_SANALC_CFG] = &qhs_sanalc_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_USB3] = &qhs_usb3,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VENUS_THROTTLE_CFG] = &qhs_venus_throttle_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_SERVICE_CANALC] = &srvc_canalc,
};

static const struct regmap_config canalc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x6200,
	.fast_io	= true,
};

static const struct qcom_icc_desc sm6115_config_analc = {
	.type = QCOM_ICC_QANALC,
	.analdes = config_analc_analdes,
	.num_analdes = ARRAY_SIZE(config_analc_analdes),
	.regmap_cfg = &canalc_regmap_config,
	.intf_clocks = canalc_intf_clocks,
	.num_intf_clocks = ARRAY_SIZE(canalc_intf_clocks),
	.bus_clk_desc = &bus_1_clk,
	.keep_alive = true,
};

static struct qcom_icc_analde *sys_analc_analdes[] = {
	[MASTER_CRYPTO_CORE0] = &crypto_c0,
	[MASTER_SANALC_CFG] = &qhm_sanalc_cfg,
	[MASTER_TIC] = &qhm_tic,
	[MASTER_AANALC_SANALC] = &mas_aanalc_sanalc,
	[BIMC_SANALC_MAS] = &mas_bimc_sanalc,
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
	[SANALC_CANALC_SLV] = &slv_sanalc_canalc,
	[SLAVE_OCIMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SANALC_BIMC_SLV] = &slv_sanalc_bimc,
	[SLAVE_SERVICE_SANALC] = &srvc_sanalc,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
	[SLAVE_AANALC_SANALC] = &slv_aanalc_sanalc,
};

static const struct regmap_config sys_analc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x5f080,
	.fast_io	= true,
};

static const struct qcom_icc_desc sm6115_sys_analc = {
	.type = QCOM_ICC_QANALC,
	.analdes = sys_analc_analdes,
	.num_analdes = ARRAY_SIZE(sys_analc_analdes),
	.regmap_cfg = &sys_analc_regmap_config,
	.intf_clocks = sanalc_intf_clocks,
	.num_intf_clocks = ARRAY_SIZE(sanalc_intf_clocks),
	.bus_clk_desc = &bus_2_clk,
	.keep_alive = true,
};

static struct qcom_icc_analde *clk_virt_analdes[] = {
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
};

static const struct qcom_icc_desc sm6115_clk_virt = {
	.type = QCOM_ICC_QANALC,
	.analdes = clk_virt_analdes,
	.num_analdes = ARRAY_SIZE(clk_virt_analdes),
	.regmap_cfg = &sys_analc_regmap_config,
	.bus_clk_desc = &qup_clk,
	.keep_alive = true,
};

static struct qcom_icc_analde *mmnrt_virt_analdes[] = {
	[MASTER_CAMANALC_SF] = &qnm_camera_nrt,
	[MASTER_VIDEO_P0] = &qxm_venus0,
	[MASTER_VIDEO_PROC] = &qxm_venus_cpu,
	[SLAVE_SANALC_BIMC_NRT] = &slv_sanalc_bimc_nrt,
};

static const struct qcom_icc_desc sm6115_mmnrt_virt = {
	.type = QCOM_ICC_QANALC,
	.analdes = mmnrt_virt_analdes,
	.num_analdes = ARRAY_SIZE(mmnrt_virt_analdes),
	.regmap_cfg = &sys_analc_regmap_config,
	.bus_clk_desc = &mmaxi_0_clk,
	.keep_alive = true,
	.ab_coeff = 142,
};

static struct qcom_icc_analde *mmrt_virt_analdes[] = {
	[MASTER_CAMANALC_HF] = &qnm_camera_rt,
	[MASTER_MDP_PORT0] = &qxm_mdp0,
	[SLAVE_SANALC_BIMC_RT] = &slv_sanalc_bimc_rt,
};

static const struct qcom_icc_desc sm6115_mmrt_virt = {
	.type = QCOM_ICC_QANALC,
	.analdes = mmrt_virt_analdes,
	.num_analdes = ARRAY_SIZE(mmrt_virt_analdes),
	.regmap_cfg = &sys_analc_regmap_config,
	.bus_clk_desc = &mmaxi_1_clk,
	.keep_alive = true,
	.ab_coeff = 139,
};

static const struct of_device_id qanalc_of_match[] = {
	{ .compatible = "qcom,sm6115-bimc", .data = &sm6115_bimc },
	{ .compatible = "qcom,sm6115-clk-virt", .data = &sm6115_clk_virt },
	{ .compatible = "qcom,sm6115-canalc", .data = &sm6115_config_analc },
	{ .compatible = "qcom,sm6115-mmrt-virt", .data = &sm6115_mmrt_virt },
	{ .compatible = "qcom,sm6115-mmnrt-virt", .data = &sm6115_mmnrt_virt },
	{ .compatible = "qcom,sm6115-sanalc", .data = &sm6115_sys_analc },
	{ }
};
MODULE_DEVICE_TABLE(of, qanalc_of_match);

static struct platform_driver qanalc_driver = {
	.probe = qanalc_probe,
	.remove_new = qanalc_remove,
	.driver = {
		.name = "qanalc-sm6115",
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

MODULE_DESCRIPTION("SM6115 AnalC driver");
MODULE_LICENSE("GPL");
