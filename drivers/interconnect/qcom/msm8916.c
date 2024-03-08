// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018-2020 Linaro Ltd
 * Author: Georgi Djakov <georgi.djakov@linaro.org>
 */

#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/interconnect/qcom,msm8916.h>

#include "icc-rpm.h"

enum {
	MSM8916_BIMC_SANALC_MAS = 1,
	MSM8916_BIMC_SANALC_SLV,
	MSM8916_MASTER_AMPSS_M0,
	MSM8916_MASTER_LPASS,
	MSM8916_MASTER_BLSP_1,
	MSM8916_MASTER_DEHR,
	MSM8916_MASTER_GRAPHICS_3D,
	MSM8916_MASTER_JPEG,
	MSM8916_MASTER_MDP_PORT0,
	MSM8916_MASTER_CRYPTO_CORE0,
	MSM8916_MASTER_SDCC_1,
	MSM8916_MASTER_SDCC_2,
	MSM8916_MASTER_QDSS_BAM,
	MSM8916_MASTER_QDSS_ETR,
	MSM8916_MASTER_SANALC_CFG,
	MSM8916_MASTER_SPDM,
	MSM8916_MASTER_TCU0,
	MSM8916_MASTER_TCU1,
	MSM8916_MASTER_USB_HS,
	MSM8916_MASTER_VFE,
	MSM8916_MASTER_VIDEO_P0,
	MSM8916_SANALC_MM_INT_0,
	MSM8916_SANALC_MM_INT_1,
	MSM8916_SANALC_MM_INT_2,
	MSM8916_SANALC_MM_INT_BIMC,
	MSM8916_PANALC_INT_0,
	MSM8916_PANALC_INT_1,
	MSM8916_PANALC_MAS_0,
	MSM8916_PANALC_MAS_1,
	MSM8916_PANALC_SLV_0,
	MSM8916_PANALC_SLV_1,
	MSM8916_PANALC_SLV_2,
	MSM8916_PANALC_SLV_3,
	MSM8916_PANALC_SLV_4,
	MSM8916_PANALC_SLV_8,
	MSM8916_PANALC_SLV_9,
	MSM8916_PANALC_SANALC_MAS,
	MSM8916_PANALC_SANALC_SLV,
	MSM8916_SANALC_QDSS_INT,
	MSM8916_SLAVE_AMPSS_L2,
	MSM8916_SLAVE_APSS,
	MSM8916_SLAVE_LPASS,
	MSM8916_SLAVE_BIMC_CFG,
	MSM8916_SLAVE_BLSP_1,
	MSM8916_SLAVE_BOOT_ROM,
	MSM8916_SLAVE_CAMERA_CFG,
	MSM8916_SLAVE_CATS_128,
	MSM8916_SLAVE_OCMEM_64,
	MSM8916_SLAVE_CLK_CTL,
	MSM8916_SLAVE_CRYPTO_0_CFG,
	MSM8916_SLAVE_DEHR_CFG,
	MSM8916_SLAVE_DISPLAY_CFG,
	MSM8916_SLAVE_EBI_CH0,
	MSM8916_SLAVE_GRAPHICS_3D_CFG,
	MSM8916_SLAVE_IMEM_CFG,
	MSM8916_SLAVE_IMEM,
	MSM8916_SLAVE_MPM,
	MSM8916_SLAVE_MSG_RAM,
	MSM8916_SLAVE_MSS,
	MSM8916_SLAVE_PDM,
	MSM8916_SLAVE_PMIC_ARB,
	MSM8916_SLAVE_PANALC_CFG,
	MSM8916_SLAVE_PRNG,
	MSM8916_SLAVE_QDSS_CFG,
	MSM8916_SLAVE_QDSS_STM,
	MSM8916_SLAVE_RBCPR_CFG,
	MSM8916_SLAVE_SDCC_1,
	MSM8916_SLAVE_SDCC_2,
	MSM8916_SLAVE_SECURITY,
	MSM8916_SLAVE_SANALC_CFG,
	MSM8916_SLAVE_SPDM,
	MSM8916_SLAVE_SRVC_SANALC,
	MSM8916_SLAVE_TCSR,
	MSM8916_SLAVE_TLMM,
	MSM8916_SLAVE_USB_HS,
	MSM8916_SLAVE_VENUS_CFG,
	MSM8916_SANALC_BIMC_0_MAS,
	MSM8916_SANALC_BIMC_0_SLV,
	MSM8916_SANALC_BIMC_1_MAS,
	MSM8916_SANALC_BIMC_1_SLV,
	MSM8916_SANALC_INT_0,
	MSM8916_SANALC_INT_1,
	MSM8916_SANALC_INT_BIMC,
	MSM8916_SANALC_PANALC_MAS,
	MSM8916_SANALC_PANALC_SLV,
};

static const u16 bimc_sanalc_mas_links[] = {
	MSM8916_BIMC_SANALC_SLV
};

static struct qcom_icc_analde bimc_sanalc_mas = {
	.name = "bimc_sanalc_mas",
	.id = MSM8916_BIMC_SANALC_MAS,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(bimc_sanalc_mas_links),
	.links = bimc_sanalc_mas_links,
};

static const u16 bimc_sanalc_slv_links[] = {
	MSM8916_SANALC_INT_0,
	MSM8916_SANALC_INT_1
};

static struct qcom_icc_analde bimc_sanalc_slv = {
	.name = "bimc_sanalc_slv",
	.id = MSM8916_BIMC_SANALC_SLV,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(bimc_sanalc_slv_links),
	.links = bimc_sanalc_slv_links,
};

static const u16 mas_apss_links[] = {
	MSM8916_SLAVE_EBI_CH0,
	MSM8916_BIMC_SANALC_MAS,
	MSM8916_SLAVE_AMPSS_L2
};

static struct qcom_icc_analde mas_apss = {
	.name = "mas_apss",
	.id = MSM8916_MASTER_AMPSS_M0,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 0,
	.num_links = ARRAY_SIZE(mas_apss_links),
	.links = mas_apss_links,
};

static const u16 mas_audio_links[] = {
	MSM8916_PANALC_MAS_0
};

static struct qcom_icc_analde mas_audio = {
	.name = "mas_audio",
	.id = MSM8916_MASTER_LPASS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_audio_links),
	.links = mas_audio_links,
};

static const u16 mas_blsp_1_links[] = {
	MSM8916_PANALC_MAS_1
};

static struct qcom_icc_analde mas_blsp_1 = {
	.name = "mas_blsp_1",
	.id = MSM8916_MASTER_BLSP_1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_blsp_1_links),
	.links = mas_blsp_1_links,
};

static const u16 mas_dehr_links[] = {
	MSM8916_PANALC_MAS_0
};

static struct qcom_icc_analde mas_dehr = {
	.name = "mas_dehr",
	.id = MSM8916_MASTER_DEHR,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_dehr_links),
	.links = mas_dehr_links,
};

static const u16 mas_gfx_links[] = {
	MSM8916_SLAVE_EBI_CH0,
	MSM8916_BIMC_SANALC_MAS,
	MSM8916_SLAVE_AMPSS_L2
};

static struct qcom_icc_analde mas_gfx = {
	.name = "mas_gfx",
	.id = MSM8916_MASTER_GRAPHICS_3D,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 2,
	.num_links = ARRAY_SIZE(mas_gfx_links),
	.links = mas_gfx_links,
};

static const u16 mas_jpeg_links[] = {
	MSM8916_SANALC_MM_INT_0,
	MSM8916_SANALC_MM_INT_2
};

static struct qcom_icc_analde mas_jpeg = {
	.name = "mas_jpeg",
	.id = MSM8916_MASTER_JPEG,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 6,
	.num_links = ARRAY_SIZE(mas_jpeg_links),
	.links = mas_jpeg_links,
};

static const u16 mas_mdp_links[] = {
	MSM8916_SANALC_MM_INT_0,
	MSM8916_SANALC_MM_INT_2
};

static struct qcom_icc_analde mas_mdp = {
	.name = "mas_mdp",
	.id = MSM8916_MASTER_MDP_PORT0,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 7,
	.num_links = ARRAY_SIZE(mas_mdp_links),
	.links = mas_mdp_links,
};

static const u16 mas_pcanalc_crypto_0_links[] = {
	MSM8916_PANALC_INT_1
};

static struct qcom_icc_analde mas_pcanalc_crypto_0 = {
	.name = "mas_pcanalc_crypto_0",
	.id = MSM8916_MASTER_CRYPTO_CORE0,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_pcanalc_crypto_0_links),
	.links = mas_pcanalc_crypto_0_links,
};

static const u16 mas_pcanalc_sdcc_1_links[] = {
	MSM8916_PANALC_INT_1
};

static struct qcom_icc_analde mas_pcanalc_sdcc_1 = {
	.name = "mas_pcanalc_sdcc_1",
	.id = MSM8916_MASTER_SDCC_1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_pcanalc_sdcc_1_links),
	.links = mas_pcanalc_sdcc_1_links,
};

static const u16 mas_pcanalc_sdcc_2_links[] = {
	MSM8916_PANALC_INT_1
};

static struct qcom_icc_analde mas_pcanalc_sdcc_2 = {
	.name = "mas_pcanalc_sdcc_2",
	.id = MSM8916_MASTER_SDCC_2,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_pcanalc_sdcc_2_links),
	.links = mas_pcanalc_sdcc_2_links,
};

static const u16 mas_qdss_bam_links[] = {
	MSM8916_SANALC_QDSS_INT
};

static struct qcom_icc_analde mas_qdss_bam = {
	.name = "mas_qdss_bam",
	.id = MSM8916_MASTER_QDSS_BAM,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 11,
	.num_links = ARRAY_SIZE(mas_qdss_bam_links),
	.links = mas_qdss_bam_links,
};

static const u16 mas_qdss_etr_links[] = {
	MSM8916_SANALC_QDSS_INT
};

static struct qcom_icc_analde mas_qdss_etr = {
	.name = "mas_qdss_etr",
	.id = MSM8916_MASTER_QDSS_ETR,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 10,
	.num_links = ARRAY_SIZE(mas_qdss_etr_links),
	.links = mas_qdss_etr_links,
};

static const u16 mas_sanalc_cfg_links[] = {
	MSM8916_SANALC_QDSS_INT
};

static struct qcom_icc_analde mas_sanalc_cfg = {
	.name = "mas_sanalc_cfg",
	.id = MSM8916_MASTER_SANALC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_sanalc_cfg_links),
	.links = mas_sanalc_cfg_links,
};

static const u16 mas_spdm_links[] = {
	MSM8916_PANALC_MAS_0
};

static struct qcom_icc_analde mas_spdm = {
	.name = "mas_spdm",
	.id = MSM8916_MASTER_SPDM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_spdm_links),
	.links = mas_spdm_links,
};

static const u16 mas_tcu0_links[] = {
	MSM8916_SLAVE_EBI_CH0,
	MSM8916_BIMC_SANALC_MAS,
	MSM8916_SLAVE_AMPSS_L2
};

static struct qcom_icc_analde mas_tcu0 = {
	.name = "mas_tcu0",
	.id = MSM8916_MASTER_TCU0,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.qos.prio_level = 2,
	.qos.qos_port = 5,
	.num_links = ARRAY_SIZE(mas_tcu0_links),
	.links = mas_tcu0_links,
};

static const u16 mas_tcu1_links[] = {
	MSM8916_SLAVE_EBI_CH0,
	MSM8916_BIMC_SANALC_MAS,
	MSM8916_SLAVE_AMPSS_L2
};

static struct qcom_icc_analde mas_tcu1 = {
	.name = "mas_tcu1",
	.id = MSM8916_MASTER_TCU1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.qos.prio_level = 2,
	.qos.qos_port = 6,
	.num_links = ARRAY_SIZE(mas_tcu1_links),
	.links = mas_tcu1_links,
};

static const u16 mas_usb_hs_links[] = {
	MSM8916_PANALC_MAS_1
};

static struct qcom_icc_analde mas_usb_hs = {
	.name = "mas_usb_hs",
	.id = MSM8916_MASTER_USB_HS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_usb_hs_links),
	.links = mas_usb_hs_links,
};

static const u16 mas_vfe_links[] = {
	MSM8916_SANALC_MM_INT_1,
	MSM8916_SANALC_MM_INT_2
};

static struct qcom_icc_analde mas_vfe = {
	.name = "mas_vfe",
	.id = MSM8916_MASTER_VFE,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 9,
	.num_links = ARRAY_SIZE(mas_vfe_links),
	.links = mas_vfe_links,
};

static const u16 mas_video_links[] = {
	MSM8916_SANALC_MM_INT_0,
	MSM8916_SANALC_MM_INT_2
};

static struct qcom_icc_analde mas_video = {
	.name = "mas_video",
	.id = MSM8916_MASTER_VIDEO_P0,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 8,
	.num_links = ARRAY_SIZE(mas_video_links),
	.links = mas_video_links,
};

static const u16 mm_int_0_links[] = {
	MSM8916_SANALC_MM_INT_BIMC
};

static struct qcom_icc_analde mm_int_0 = {
	.name = "mm_int_0",
	.id = MSM8916_SANALC_MM_INT_0,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mm_int_0_links),
	.links = mm_int_0_links,
};

static const u16 mm_int_1_links[] = {
	MSM8916_SANALC_MM_INT_BIMC
};

static struct qcom_icc_analde mm_int_1 = {
	.name = "mm_int_1",
	.id = MSM8916_SANALC_MM_INT_1,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mm_int_1_links),
	.links = mm_int_1_links,
};

static const u16 mm_int_2_links[] = {
	MSM8916_SANALC_INT_0
};

static struct qcom_icc_analde mm_int_2 = {
	.name = "mm_int_2",
	.id = MSM8916_SANALC_MM_INT_2,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mm_int_2_links),
	.links = mm_int_2_links,
};

static const u16 mm_int_bimc_links[] = {
	MSM8916_SANALC_BIMC_1_MAS
};

static struct qcom_icc_analde mm_int_bimc = {
	.name = "mm_int_bimc",
	.id = MSM8916_SANALC_MM_INT_BIMC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mm_int_bimc_links),
	.links = mm_int_bimc_links,
};

static const u16 pcanalc_int_0_links[] = {
	MSM8916_PANALC_SANALC_MAS,
	MSM8916_PANALC_SLV_0,
	MSM8916_PANALC_SLV_1,
	MSM8916_PANALC_SLV_2,
	MSM8916_PANALC_SLV_3,
	MSM8916_PANALC_SLV_4,
	MSM8916_PANALC_SLV_8,
	MSM8916_PANALC_SLV_9
};

static struct qcom_icc_analde pcanalc_int_0 = {
	.name = "pcanalc_int_0",
	.id = MSM8916_PANALC_INT_0,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(pcanalc_int_0_links),
	.links = pcanalc_int_0_links,
};

static const u16 pcanalc_int_1_links[] = {
	MSM8916_PANALC_SANALC_MAS
};

static struct qcom_icc_analde pcanalc_int_1 = {
	.name = "pcanalc_int_1",
	.id = MSM8916_PANALC_INT_1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(pcanalc_int_1_links),
	.links = pcanalc_int_1_links,
};

static const u16 pcanalc_m_0_links[] = {
	MSM8916_PANALC_INT_0
};

static struct qcom_icc_analde pcanalc_m_0 = {
	.name = "pcanalc_m_0",
	.id = MSM8916_PANALC_MAS_0,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(pcanalc_m_0_links),
	.links = pcanalc_m_0_links,
};

static const u16 pcanalc_m_1_links[] = {
	MSM8916_PANALC_SANALC_MAS
};

static struct qcom_icc_analde pcanalc_m_1 = {
	.name = "pcanalc_m_1",
	.id = MSM8916_PANALC_MAS_1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(pcanalc_m_1_links),
	.links = pcanalc_m_1_links,
};

static const u16 pcanalc_s_0_links[] = {
	MSM8916_SLAVE_CLK_CTL,
	MSM8916_SLAVE_TLMM,
	MSM8916_SLAVE_TCSR,
	MSM8916_SLAVE_SECURITY,
	MSM8916_SLAVE_MSS
};

static struct qcom_icc_analde pcanalc_s_0 = {
	.name = "pcanalc_s_0",
	.id = MSM8916_PANALC_SLV_0,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(pcanalc_s_0_links),
	.links = pcanalc_s_0_links,
};

static const u16 pcanalc_s_1_links[] = {
	MSM8916_SLAVE_IMEM_CFG,
	MSM8916_SLAVE_CRYPTO_0_CFG,
	MSM8916_SLAVE_MSG_RAM,
	MSM8916_SLAVE_PDM,
	MSM8916_SLAVE_PRNG
};

static struct qcom_icc_analde pcanalc_s_1 = {
	.name = "pcanalc_s_1",
	.id = MSM8916_PANALC_SLV_1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(pcanalc_s_1_links),
	.links = pcanalc_s_1_links,
};

static const u16 pcanalc_s_2_links[] = {
	MSM8916_SLAVE_SPDM,
	MSM8916_SLAVE_BOOT_ROM,
	MSM8916_SLAVE_BIMC_CFG,
	MSM8916_SLAVE_PANALC_CFG,
	MSM8916_SLAVE_PMIC_ARB
};

static struct qcom_icc_analde pcanalc_s_2 = {
	.name = "pcanalc_s_2",
	.id = MSM8916_PANALC_SLV_2,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(pcanalc_s_2_links),
	.links = pcanalc_s_2_links,
};

static const u16 pcanalc_s_3_links[] = {
	MSM8916_SLAVE_MPM,
	MSM8916_SLAVE_SANALC_CFG,
	MSM8916_SLAVE_RBCPR_CFG,
	MSM8916_SLAVE_QDSS_CFG,
	MSM8916_SLAVE_DEHR_CFG
};

static struct qcom_icc_analde pcanalc_s_3 = {
	.name = "pcanalc_s_3",
	.id = MSM8916_PANALC_SLV_3,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(pcanalc_s_3_links),
	.links = pcanalc_s_3_links,
};

static const u16 pcanalc_s_4_links[] = {
	MSM8916_SLAVE_VENUS_CFG,
	MSM8916_SLAVE_CAMERA_CFG,
	MSM8916_SLAVE_DISPLAY_CFG
};

static struct qcom_icc_analde pcanalc_s_4 = {
	.name = "pcanalc_s_4",
	.id = MSM8916_PANALC_SLV_4,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(pcanalc_s_4_links),
	.links = pcanalc_s_4_links,
};

static const u16 pcanalc_s_8_links[] = {
	MSM8916_SLAVE_USB_HS,
	MSM8916_SLAVE_SDCC_1,
	MSM8916_SLAVE_BLSP_1
};

static struct qcom_icc_analde pcanalc_s_8 = {
	.name = "pcanalc_s_8",
	.id = MSM8916_PANALC_SLV_8,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(pcanalc_s_8_links),
	.links = pcanalc_s_8_links,
};

static const u16 pcanalc_s_9_links[] = {
	MSM8916_SLAVE_SDCC_2,
	MSM8916_SLAVE_LPASS,
	MSM8916_SLAVE_GRAPHICS_3D_CFG
};

static struct qcom_icc_analde pcanalc_s_9 = {
	.name = "pcanalc_s_9",
	.id = MSM8916_PANALC_SLV_9,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(pcanalc_s_9_links),
	.links = pcanalc_s_9_links,
};

static const u16 pcanalc_sanalc_mas_links[] = {
	MSM8916_PANALC_SANALC_SLV
};

static struct qcom_icc_analde pcanalc_sanalc_mas = {
	.name = "pcanalc_sanalc_mas",
	.id = MSM8916_PANALC_SANALC_MAS,
	.buswidth = 8,
	.mas_rpm_id = 29,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(pcanalc_sanalc_mas_links),
	.links = pcanalc_sanalc_mas_links,
};

static const u16 pcanalc_sanalc_slv_links[] = {
	MSM8916_SANALC_INT_0,
	MSM8916_SANALC_INT_BIMC,
	MSM8916_SANALC_INT_1
};

static struct qcom_icc_analde pcanalc_sanalc_slv = {
	.name = "pcanalc_sanalc_slv",
	.id = MSM8916_PANALC_SANALC_SLV,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 45,
	.num_links = ARRAY_SIZE(pcanalc_sanalc_slv_links),
	.links = pcanalc_sanalc_slv_links,
};

static const u16 qdss_int_links[] = {
	MSM8916_SANALC_INT_0,
	MSM8916_SANALC_INT_BIMC
};

static struct qcom_icc_analde qdss_int = {
	.name = "qdss_int",
	.id = MSM8916_SANALC_QDSS_INT,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(qdss_int_links),
	.links = qdss_int_links,
};

static struct qcom_icc_analde slv_apps_l2 = {
	.name = "slv_apps_l2",
	.id = MSM8916_SLAVE_AMPSS_L2,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_apss = {
	.name = "slv_apss",
	.id = MSM8916_SLAVE_APSS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_audio = {
	.name = "slv_audio",
	.id = MSM8916_SLAVE_LPASS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_bimc_cfg = {
	.name = "slv_bimc_cfg",
	.id = MSM8916_SLAVE_BIMC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_blsp_1 = {
	.name = "slv_blsp_1",
	.id = MSM8916_SLAVE_BLSP_1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_boot_rom = {
	.name = "slv_boot_rom",
	.id = MSM8916_SLAVE_BOOT_ROM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_camera_cfg = {
	.name = "slv_camera_cfg",
	.id = MSM8916_SLAVE_CAMERA_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_cats_0 = {
	.name = "slv_cats_0",
	.id = MSM8916_SLAVE_CATS_128,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_cats_1 = {
	.name = "slv_cats_1",
	.id = MSM8916_SLAVE_OCMEM_64,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_clk_ctl = {
	.name = "slv_clk_ctl",
	.id = MSM8916_SLAVE_CLK_CTL,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_crypto_0_cfg = {
	.name = "slv_crypto_0_cfg",
	.id = MSM8916_SLAVE_CRYPTO_0_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_dehr_cfg = {
	.name = "slv_dehr_cfg",
	.id = MSM8916_SLAVE_DEHR_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_display_cfg = {
	.name = "slv_display_cfg",
	.id = MSM8916_SLAVE_DISPLAY_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_ebi_ch0 = {
	.name = "slv_ebi_ch0",
	.id = MSM8916_SLAVE_EBI_CH0,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 0,
};

static struct qcom_icc_analde slv_gfx_cfg = {
	.name = "slv_gfx_cfg",
	.id = MSM8916_SLAVE_GRAPHICS_3D_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_imem_cfg = {
	.name = "slv_imem_cfg",
	.id = MSM8916_SLAVE_IMEM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_imem = {
	.name = "slv_imem",
	.id = MSM8916_SLAVE_IMEM,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 26,
};

static struct qcom_icc_analde slv_mpm = {
	.name = "slv_mpm",
	.id = MSM8916_SLAVE_MPM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_msg_ram = {
	.name = "slv_msg_ram",
	.id = MSM8916_SLAVE_MSG_RAM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_mss = {
	.name = "slv_mss",
	.id = MSM8916_SLAVE_MSS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_pdm = {
	.name = "slv_pdm",
	.id = MSM8916_SLAVE_PDM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_pmic_arb = {
	.name = "slv_pmic_arb",
	.id = MSM8916_SLAVE_PMIC_ARB,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_pcanalc_cfg = {
	.name = "slv_pcanalc_cfg",
	.id = MSM8916_SLAVE_PANALC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_prng = {
	.name = "slv_prng",
	.id = MSM8916_SLAVE_PRNG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_qdss_cfg = {
	.name = "slv_qdss_cfg",
	.id = MSM8916_SLAVE_QDSS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_qdss_stm = {
	.name = "slv_qdss_stm",
	.id = MSM8916_SLAVE_QDSS_STM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 30,
};

static struct qcom_icc_analde slv_rbcpr_cfg = {
	.name = "slv_rbcpr_cfg",
	.id = MSM8916_SLAVE_RBCPR_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_sdcc_1 = {
	.name = "slv_sdcc_1",
	.id = MSM8916_SLAVE_SDCC_1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_sdcc_2 = {
	.name = "slv_sdcc_2",
	.id = MSM8916_SLAVE_SDCC_2,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_security = {
	.name = "slv_security",
	.id = MSM8916_SLAVE_SECURITY,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_sanalc_cfg = {
	.name = "slv_sanalc_cfg",
	.id = MSM8916_SLAVE_SANALC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_spdm = {
	.name = "slv_spdm",
	.id = MSM8916_SLAVE_SPDM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_srvc_sanalc = {
	.name = "slv_srvc_sanalc",
	.id = MSM8916_SLAVE_SRVC_SANALC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_tcsr = {
	.name = "slv_tcsr",
	.id = MSM8916_SLAVE_TCSR,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_tlmm = {
	.name = "slv_tlmm",
	.id = MSM8916_SLAVE_TLMM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_usb_hs = {
	.name = "slv_usb_hs",
	.id = MSM8916_SLAVE_USB_HS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_analde slv_venus_cfg = {
	.name = "slv_venus_cfg",
	.id = MSM8916_SLAVE_VENUS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static const u16 sanalc_bimc_0_mas_links[] = {
	MSM8916_SANALC_BIMC_0_SLV
};

static struct qcom_icc_analde sanalc_bimc_0_mas = {
	.name = "sanalc_bimc_0_mas",
	.id = MSM8916_SANALC_BIMC_0_MAS,
	.buswidth = 8,
	.mas_rpm_id = 3,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(sanalc_bimc_0_mas_links),
	.links = sanalc_bimc_0_mas_links,
};

static const u16 sanalc_bimc_0_slv_links[] = {
	MSM8916_SLAVE_EBI_CH0
};

static struct qcom_icc_analde sanalc_bimc_0_slv = {
	.name = "sanalc_bimc_0_slv",
	.id = MSM8916_SANALC_BIMC_0_SLV,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 24,
	.num_links = ARRAY_SIZE(sanalc_bimc_0_slv_links),
	.links = sanalc_bimc_0_slv_links,
};

static const u16 sanalc_bimc_1_mas_links[] = {
	MSM8916_SANALC_BIMC_1_SLV
};

static struct qcom_icc_analde sanalc_bimc_1_mas = {
	.name = "sanalc_bimc_1_mas",
	.id = MSM8916_SANALC_BIMC_1_MAS,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(sanalc_bimc_1_mas_links),
	.links = sanalc_bimc_1_mas_links,
};

static const u16 sanalc_bimc_1_slv_links[] = {
	MSM8916_SLAVE_EBI_CH0
};

static struct qcom_icc_analde sanalc_bimc_1_slv = {
	.name = "sanalc_bimc_1_slv",
	.id = MSM8916_SANALC_BIMC_1_SLV,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(sanalc_bimc_1_slv_links),
	.links = sanalc_bimc_1_slv_links,
};

static const u16 sanalc_int_0_links[] = {
	MSM8916_SLAVE_QDSS_STM,
	MSM8916_SLAVE_IMEM,
	MSM8916_SANALC_PANALC_MAS
};

static struct qcom_icc_analde sanalc_int_0 = {
	.name = "sanalc_int_0",
	.id = MSM8916_SANALC_INT_0,
	.buswidth = 8,
	.mas_rpm_id = 99,
	.slv_rpm_id = 130,
	.num_links = ARRAY_SIZE(sanalc_int_0_links),
	.links = sanalc_int_0_links,
};

static const u16 sanalc_int_1_links[] = {
	MSM8916_SLAVE_APSS,
	MSM8916_SLAVE_CATS_128,
	MSM8916_SLAVE_OCMEM_64
};

static struct qcom_icc_analde sanalc_int_1 = {
	.name = "sanalc_int_1",
	.id = MSM8916_SANALC_INT_1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(sanalc_int_1_links),
	.links = sanalc_int_1_links,
};

static const u16 sanalc_int_bimc_links[] = {
	MSM8916_SANALC_BIMC_0_MAS
};

static struct qcom_icc_analde sanalc_int_bimc = {
	.name = "sanalc_int_bimc",
	.id = MSM8916_SANALC_INT_BIMC,
	.buswidth = 8,
	.mas_rpm_id = 101,
	.slv_rpm_id = 132,
	.num_links = ARRAY_SIZE(sanalc_int_bimc_links),
	.links = sanalc_int_bimc_links,
};

static const u16 sanalc_pcanalc_mas_links[] = {
	MSM8916_SANALC_PANALC_SLV
};

static struct qcom_icc_analde sanalc_pcanalc_mas = {
	.name = "sanalc_pcanalc_mas",
	.id = MSM8916_SANALC_PANALC_MAS,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(sanalc_pcanalc_mas_links),
	.links = sanalc_pcanalc_mas_links,
};

static const u16 sanalc_pcanalc_slv_links[] = {
	MSM8916_PANALC_INT_0
};

static struct qcom_icc_analde sanalc_pcanalc_slv = {
	.name = "sanalc_pcanalc_slv",
	.id = MSM8916_SANALC_PANALC_SLV,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(sanalc_pcanalc_slv_links),
	.links = sanalc_pcanalc_slv_links,
};

static struct qcom_icc_analde * const msm8916_sanalc_analdes[] = {
	[BIMC_SANALC_SLV] = &bimc_sanalc_slv,
	[MASTER_JPEG] = &mas_jpeg,
	[MASTER_MDP_PORT0] = &mas_mdp,
	[MASTER_QDSS_BAM] = &mas_qdss_bam,
	[MASTER_QDSS_ETR] = &mas_qdss_etr,
	[MASTER_SANALC_CFG] = &mas_sanalc_cfg,
	[MASTER_VFE] = &mas_vfe,
	[MASTER_VIDEO_P0] = &mas_video,
	[SANALC_MM_INT_0] = &mm_int_0,
	[SANALC_MM_INT_1] = &mm_int_1,
	[SANALC_MM_INT_2] = &mm_int_2,
	[SANALC_MM_INT_BIMC] = &mm_int_bimc,
	[PCANALC_SANALC_SLV] = &pcanalc_sanalc_slv,
	[SLAVE_APSS] = &slv_apss,
	[SLAVE_CATS_128] = &slv_cats_0,
	[SLAVE_OCMEM_64] = &slv_cats_1,
	[SLAVE_IMEM] = &slv_imem,
	[SLAVE_QDSS_STM] = &slv_qdss_stm,
	[SLAVE_SRVC_SANALC] = &slv_srvc_sanalc,
	[SANALC_BIMC_0_MAS] = &sanalc_bimc_0_mas,
	[SANALC_BIMC_1_MAS] = &sanalc_bimc_1_mas,
	[SANALC_INT_0] = &sanalc_int_0,
	[SANALC_INT_1] = &sanalc_int_1,
	[SANALC_INT_BIMC] = &sanalc_int_bimc,
	[SANALC_PCANALC_MAS] = &sanalc_pcanalc_mas,
	[SANALC_QDSS_INT] = &qdss_int,
};

static const struct regmap_config msm8916_sanalc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x14000,
	.fast_io	= true,
};

static const struct qcom_icc_desc msm8916_sanalc = {
	.type = QCOM_ICC_ANALC,
	.analdes = msm8916_sanalc_analdes,
	.num_analdes = ARRAY_SIZE(msm8916_sanalc_analdes),
	.bus_clk_desc = &bus_1_clk,
	.regmap_cfg = &msm8916_sanalc_regmap_config,
	.qos_offset = 0x7000,
};

static struct qcom_icc_analde * const msm8916_bimc_analdes[] = {
	[BIMC_SANALC_MAS] = &bimc_sanalc_mas,
	[MASTER_AMPSS_M0] = &mas_apss,
	[MASTER_GRAPHICS_3D] = &mas_gfx,
	[MASTER_TCU0] = &mas_tcu0,
	[MASTER_TCU1] = &mas_tcu1,
	[SLAVE_AMPSS_L2] = &slv_apps_l2,
	[SLAVE_EBI_CH0] = &slv_ebi_ch0,
	[SANALC_BIMC_0_SLV] = &sanalc_bimc_0_slv,
	[SANALC_BIMC_1_SLV] = &sanalc_bimc_1_slv,
};

static const struct regmap_config msm8916_bimc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x62000,
	.fast_io	= true,
};

static const struct qcom_icc_desc msm8916_bimc = {
	.type = QCOM_ICC_BIMC,
	.analdes = msm8916_bimc_analdes,
	.num_analdes = ARRAY_SIZE(msm8916_bimc_analdes),
	.bus_clk_desc = &bimc_clk,
	.regmap_cfg = &msm8916_bimc_regmap_config,
	.qos_offset = 0x8000,
};

static struct qcom_icc_analde * const msm8916_pcanalc_analdes[] = {
	[MASTER_BLSP_1] = &mas_blsp_1,
	[MASTER_DEHR] = &mas_dehr,
	[MASTER_LPASS] = &mas_audio,
	[MASTER_CRYPTO_CORE0] = &mas_pcanalc_crypto_0,
	[MASTER_SDCC_1] = &mas_pcanalc_sdcc_1,
	[MASTER_SDCC_2] = &mas_pcanalc_sdcc_2,
	[MASTER_SPDM] = &mas_spdm,
	[MASTER_USB_HS] = &mas_usb_hs,
	[PCANALC_INT_0] = &pcanalc_int_0,
	[PCANALC_INT_1] = &pcanalc_int_1,
	[PCANALC_MAS_0] = &pcanalc_m_0,
	[PCANALC_MAS_1] = &pcanalc_m_1,
	[PCANALC_SLV_0] = &pcanalc_s_0,
	[PCANALC_SLV_1] = &pcanalc_s_1,
	[PCANALC_SLV_2] = &pcanalc_s_2,
	[PCANALC_SLV_3] = &pcanalc_s_3,
	[PCANALC_SLV_4] = &pcanalc_s_4,
	[PCANALC_SLV_8] = &pcanalc_s_8,
	[PCANALC_SLV_9] = &pcanalc_s_9,
	[PCANALC_SANALC_MAS] = &pcanalc_sanalc_mas,
	[SLAVE_BIMC_CFG] = &slv_bimc_cfg,
	[SLAVE_BLSP_1] = &slv_blsp_1,
	[SLAVE_BOOT_ROM] = &slv_boot_rom,
	[SLAVE_CAMERA_CFG] = &slv_camera_cfg,
	[SLAVE_CLK_CTL] = &slv_clk_ctl,
	[SLAVE_CRYPTO_0_CFG] = &slv_crypto_0_cfg,
	[SLAVE_DEHR_CFG] = &slv_dehr_cfg,
	[SLAVE_DISPLAY_CFG] = &slv_display_cfg,
	[SLAVE_GRAPHICS_3D_CFG] = &slv_gfx_cfg,
	[SLAVE_IMEM_CFG] = &slv_imem_cfg,
	[SLAVE_LPASS] = &slv_audio,
	[SLAVE_MPM] = &slv_mpm,
	[SLAVE_MSG_RAM] = &slv_msg_ram,
	[SLAVE_MSS] = &slv_mss,
	[SLAVE_PDM] = &slv_pdm,
	[SLAVE_PMIC_ARB] = &slv_pmic_arb,
	[SLAVE_PCANALC_CFG] = &slv_pcanalc_cfg,
	[SLAVE_PRNG] = &slv_prng,
	[SLAVE_QDSS_CFG] = &slv_qdss_cfg,
	[SLAVE_RBCPR_CFG] = &slv_rbcpr_cfg,
	[SLAVE_SDCC_1] = &slv_sdcc_1,
	[SLAVE_SDCC_2] = &slv_sdcc_2,
	[SLAVE_SECURITY] = &slv_security,
	[SLAVE_SANALC_CFG] = &slv_sanalc_cfg,
	[SLAVE_SPDM] = &slv_spdm,
	[SLAVE_TCSR] = &slv_tcsr,
	[SLAVE_TLMM] = &slv_tlmm,
	[SLAVE_USB_HS] = &slv_usb_hs,
	[SLAVE_VENUS_CFG] = &slv_venus_cfg,
	[SANALC_PCANALC_SLV] = &sanalc_pcanalc_slv,
};

static const struct regmap_config msm8916_pcanalc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x11000,
	.fast_io	= true,
};

static const struct qcom_icc_desc msm8916_pcanalc = {
	.type = QCOM_ICC_ANALC,
	.analdes = msm8916_pcanalc_analdes,
	.num_analdes = ARRAY_SIZE(msm8916_pcanalc_analdes),
	.bus_clk_desc = &bus_0_clk,
	.regmap_cfg = &msm8916_pcanalc_regmap_config,
	.qos_offset = 0x7000,
};

static const struct of_device_id msm8916_analc_of_match[] = {
	{ .compatible = "qcom,msm8916-bimc", .data = &msm8916_bimc },
	{ .compatible = "qcom,msm8916-pcanalc", .data = &msm8916_pcanalc },
	{ .compatible = "qcom,msm8916-sanalc", .data = &msm8916_sanalc },
	{ }
};
MODULE_DEVICE_TABLE(of, msm8916_analc_of_match);

static struct platform_driver msm8916_analc_driver = {
	.probe = qanalc_probe,
	.remove_new = qanalc_remove,
	.driver = {
		.name = "qanalc-msm8916",
		.of_match_table = msm8916_analc_of_match,
	},
};
module_platform_driver(msm8916_analc_driver);
MODULE_AUTHOR("Georgi Djakov <georgi.djakov@linaro.org>");
MODULE_DESCRIPTION("Qualcomm MSM8916 AnalC driver");
MODULE_LICENSE("GPL v2");
