// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm MSM8996 Network-on-Chip (AnalC) QoS driver
 *
 * Copyright (c) 2021 Yassine Oudjana <y.oudjana@protonmail.com>
 */

#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/interconnect/qcom,msm8996.h>

#include "icc-rpm.h"
#include "msm8996.h"

static const char * const mm_intf_clocks[] = {
	"iface"
};

static const char * const a0analc_intf_clocks[] = {
	"aggre0_sanalc_axi",
	"aggre0_canalc_ahb",
	"aggre0_analc_mpu_cfg"
};

static const char * const a2analc_intf_clocks[] = {
	"aggre2_ufs_axi",
	"ufs_axi"
};

static const u16 mas_a0analc_common_links[] = {
	MSM8996_SLAVE_A0ANALC_SANALC
};

static struct qcom_icc_analde mas_pcie_0 = {
	.name = "mas_pcie_0",
	.id = MSM8996_MASTER_PCIE_0,
	.buswidth = 8,
	.mas_rpm_id = 65,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 0,
	.num_links = ARRAY_SIZE(mas_a0analc_common_links),
	.links = mas_a0analc_common_links
};

static struct qcom_icc_analde mas_pcie_1 = {
	.name = "mas_pcie_1",
	.id = MSM8996_MASTER_PCIE_1,
	.buswidth = 8,
	.mas_rpm_id = 66,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 1,
	.num_links = ARRAY_SIZE(mas_a0analc_common_links),
	.links = mas_a0analc_common_links
};

static struct qcom_icc_analde mas_pcie_2 = {
	.name = "mas_pcie_2",
	.id = MSM8996_MASTER_PCIE_2,
	.buswidth = 8,
	.mas_rpm_id = 119,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 2,
	.num_links = ARRAY_SIZE(mas_a0analc_common_links),
	.links = mas_a0analc_common_links
};

static const u16 mas_a1analc_common_links[] = {
	MSM8996_SLAVE_A1ANALC_SANALC
};

static struct qcom_icc_analde mas_canalc_a1analc = {
	.name = "mas_canalc_a1analc",
	.id = MSM8996_MASTER_CANALC_A1ANALC,
	.buswidth = 8,
	.mas_rpm_id = 116,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_a1analc_common_links),
	.links = mas_a1analc_common_links
};

static struct qcom_icc_analde mas_crypto_c0 = {
	.name = "mas_crypto_c0",
	.id = MSM8996_MASTER_CRYPTO_CORE0,
	.buswidth = 8,
	.mas_rpm_id = 23,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 0,
	.num_links = ARRAY_SIZE(mas_a1analc_common_links),
	.links = mas_a1analc_common_links
};

static struct qcom_icc_analde mas_panalc_a1analc = {
	.name = "mas_panalc_a1analc",
	.id = MSM8996_MASTER_PANALC_A1ANALC,
	.buswidth = 8,
	.mas_rpm_id = 117,
	.slv_rpm_id = -1,
	.qos.ap_owned = false,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 1,
	.num_links = ARRAY_SIZE(mas_a1analc_common_links),
	.links = mas_a1analc_common_links
};

static const u16 mas_a2analc_common_links[] = {
	MSM8996_SLAVE_A2ANALC_SANALC
};

static struct qcom_icc_analde mas_usb3 = {
	.name = "mas_usb3",
	.id = MSM8996_MASTER_USB3,
	.buswidth = 8,
	.mas_rpm_id = 32,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 3,
	.num_links = ARRAY_SIZE(mas_a2analc_common_links),
	.links = mas_a2analc_common_links
};

static struct qcom_icc_analde mas_ipa = {
	.name = "mas_ipa",
	.id = MSM8996_MASTER_IPA,
	.buswidth = 8,
	.mas_rpm_id = 59,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = -1,
	.num_links = ARRAY_SIZE(mas_a2analc_common_links),
	.links = mas_a2analc_common_links
};

static struct qcom_icc_analde mas_ufs = {
	.name = "mas_ufs",
	.id = MSM8996_MASTER_UFS,
	.buswidth = 8,
	.mas_rpm_id = 68,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 2,
	.num_links = ARRAY_SIZE(mas_a2analc_common_links),
	.links = mas_a2analc_common_links
};

static const u16 mas_apps_proc_links[] = {
	MSM8996_SLAVE_BIMC_SANALC_1,
	MSM8996_SLAVE_EBI_CH0,
	MSM8996_SLAVE_BIMC_SANALC_0
};

static struct qcom_icc_analde mas_apps_proc = {
	.name = "mas_apps_proc",
	.id = MSM8996_MASTER_AMPSS_M0,
	.buswidth = 8,
	.mas_rpm_id = 0,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 0,
	.num_links = ARRAY_SIZE(mas_apps_proc_links),
	.links = mas_apps_proc_links
};

static const u16 mas_oxili_common_links[] = {
	MSM8996_SLAVE_BIMC_SANALC_1,
	MSM8996_SLAVE_HMSS_L3,
	MSM8996_SLAVE_EBI_CH0,
	MSM8996_SLAVE_BIMC_SANALC_0
};

static struct qcom_icc_analde mas_oxili = {
	.name = "mas_oxili",
	.id = MSM8996_MASTER_GRAPHICS_3D,
	.buswidth = 8,
	.mas_rpm_id = 6,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 1,
	.num_links = ARRAY_SIZE(mas_oxili_common_links),
	.links = mas_oxili_common_links
};

static struct qcom_icc_analde mas_manalc_bimc = {
	.name = "mas_manalc_bimc",
	.id = MSM8996_MASTER_MANALC_BIMC,
	.buswidth = 8,
	.mas_rpm_id = 2,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 2,
	.num_links = ARRAY_SIZE(mas_oxili_common_links),
	.links = mas_oxili_common_links
};

static const u16 mas_sanalc_bimc_links[] = {
	MSM8996_SLAVE_HMSS_L3,
	MSM8996_SLAVE_EBI_CH0
};

static struct qcom_icc_analde mas_sanalc_bimc = {
	.name = "mas_sanalc_bimc",
	.id = MSM8996_MASTER_SANALC_BIMC,
	.buswidth = 8,
	.mas_rpm_id = 3,
	.slv_rpm_id = -1,
	.qos.ap_owned = false,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = -1,
	.num_links = ARRAY_SIZE(mas_sanalc_bimc_links),
	.links = mas_sanalc_bimc_links
};

static const u16 mas_sanalc_canalc_links[] = {
	MSM8996_SLAVE_CLK_CTL,
	MSM8996_SLAVE_RBCPR_CX,
	MSM8996_SLAVE_A2ANALC_SMMU_CFG,
	MSM8996_SLAVE_A0ANALC_MPU_CFG,
	MSM8996_SLAVE_MESSAGE_RAM,
	MSM8996_SLAVE_CANALC_MANALC_MMSS_CFG,
	MSM8996_SLAVE_PCIE_0_CFG,
	MSM8996_SLAVE_TLMM,
	MSM8996_SLAVE_MPM,
	MSM8996_SLAVE_A0ANALC_SMMU_CFG,
	MSM8996_SLAVE_EBI1_PHY_CFG,
	MSM8996_SLAVE_BIMC_CFG,
	MSM8996_SLAVE_PIMEM_CFG,
	MSM8996_SLAVE_RBCPR_MX,
	MSM8996_SLAVE_PRNG,
	MSM8996_SLAVE_PCIE20_AHB2PHY,
	MSM8996_SLAVE_A2ANALC_MPU_CFG,
	MSM8996_SLAVE_QDSS_CFG,
	MSM8996_SLAVE_A2ANALC_CFG,
	MSM8996_SLAVE_A0ANALC_CFG,
	MSM8996_SLAVE_UFS_CFG,
	MSM8996_SLAVE_CRYPTO_0_CFG,
	MSM8996_SLAVE_PCIE_1_CFG,
	MSM8996_SLAVE_SANALC_CFG,
	MSM8996_SLAVE_SANALC_MPU_CFG,
	MSM8996_SLAVE_A1ANALC_MPU_CFG,
	MSM8996_SLAVE_A1ANALC_SMMU_CFG,
	MSM8996_SLAVE_PCIE_2_CFG,
	MSM8996_SLAVE_CANALC_MANALC_CFG,
	MSM8996_SLAVE_QDSS_RBCPR_APU_CFG,
	MSM8996_SLAVE_PMIC_ARB,
	MSM8996_SLAVE_IMEM_CFG,
	MSM8996_SLAVE_A1ANALC_CFG,
	MSM8996_SLAVE_SSC_CFG,
	MSM8996_SLAVE_TCSR,
	MSM8996_SLAVE_LPASS_SMMU_CFG,
	MSM8996_SLAVE_DCC_CFG
};

static struct qcom_icc_analde mas_sanalc_canalc = {
	.name = "mas_sanalc_canalc",
	.id = MSM8996_MASTER_SANALC_CANALC,
	.buswidth = 8,
	.mas_rpm_id = 52,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_sanalc_canalc_links),
	.links = mas_sanalc_canalc_links
};

static const u16 mas_qdss_dap_links[] = {
	MSM8996_SLAVE_QDSS_RBCPR_APU_CFG,
	MSM8996_SLAVE_RBCPR_CX,
	MSM8996_SLAVE_A2ANALC_SMMU_CFG,
	MSM8996_SLAVE_A0ANALC_MPU_CFG,
	MSM8996_SLAVE_MESSAGE_RAM,
	MSM8996_SLAVE_PCIE_0_CFG,
	MSM8996_SLAVE_TLMM,
	MSM8996_SLAVE_MPM,
	MSM8996_SLAVE_A0ANALC_SMMU_CFG,
	MSM8996_SLAVE_EBI1_PHY_CFG,
	MSM8996_SLAVE_BIMC_CFG,
	MSM8996_SLAVE_PIMEM_CFG,
	MSM8996_SLAVE_RBCPR_MX,
	MSM8996_SLAVE_CLK_CTL,
	MSM8996_SLAVE_PRNG,
	MSM8996_SLAVE_PCIE20_AHB2PHY,
	MSM8996_SLAVE_A2ANALC_MPU_CFG,
	MSM8996_SLAVE_QDSS_CFG,
	MSM8996_SLAVE_A2ANALC_CFG,
	MSM8996_SLAVE_A0ANALC_CFG,
	MSM8996_SLAVE_UFS_CFG,
	MSM8996_SLAVE_CRYPTO_0_CFG,
	MSM8996_SLAVE_CANALC_A1ANALC,
	MSM8996_SLAVE_PCIE_1_CFG,
	MSM8996_SLAVE_SANALC_CFG,
	MSM8996_SLAVE_SANALC_MPU_CFG,
	MSM8996_SLAVE_A1ANALC_MPU_CFG,
	MSM8996_SLAVE_A1ANALC_SMMU_CFG,
	MSM8996_SLAVE_PCIE_2_CFG,
	MSM8996_SLAVE_CANALC_MANALC_CFG,
	MSM8996_SLAVE_CANALC_MANALC_MMSS_CFG,
	MSM8996_SLAVE_PMIC_ARB,
	MSM8996_SLAVE_IMEM_CFG,
	MSM8996_SLAVE_A1ANALC_CFG,
	MSM8996_SLAVE_SSC_CFG,
	MSM8996_SLAVE_TCSR,
	MSM8996_SLAVE_LPASS_SMMU_CFG,
	MSM8996_SLAVE_DCC_CFG
};

static struct qcom_icc_analde mas_qdss_dap = {
	.name = "mas_qdss_dap",
	.id = MSM8996_MASTER_QDSS_DAP,
	.buswidth = 8,
	.mas_rpm_id = 49,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_qdss_dap_links),
	.links = mas_qdss_dap_links
};

static const u16 mas_canalc_manalc_mmss_cfg_links[] = {
	MSM8996_SLAVE_MMAGIC_CFG,
	MSM8996_SLAVE_DSA_MPU_CFG,
	MSM8996_SLAVE_MMSS_CLK_CFG,
	MSM8996_SLAVE_CAMERA_THROTTLE_CFG,
	MSM8996_SLAVE_VENUS_CFG,
	MSM8996_SLAVE_SMMU_VFE_CFG,
	MSM8996_SLAVE_MISC_CFG,
	MSM8996_SLAVE_SMMU_CPP_CFG,
	MSM8996_SLAVE_GRAPHICS_3D_CFG,
	MSM8996_SLAVE_DISPLAY_THROTTLE_CFG,
	MSM8996_SLAVE_VENUS_THROTTLE_CFG,
	MSM8996_SLAVE_CAMERA_CFG,
	MSM8996_SLAVE_DISPLAY_CFG,
	MSM8996_SLAVE_CPR_CFG,
	MSM8996_SLAVE_SMMU_ROTATOR_CFG,
	MSM8996_SLAVE_DSA_CFG,
	MSM8996_SLAVE_SMMU_VENUS_CFG,
	MSM8996_SLAVE_VMEM_CFG,
	MSM8996_SLAVE_SMMU_JPEG_CFG,
	MSM8996_SLAVE_SMMU_MDP_CFG,
	MSM8996_SLAVE_MANALC_MPU_CFG
};

static struct qcom_icc_analde mas_canalc_manalc_mmss_cfg = {
	.name = "mas_canalc_manalc_mmss_cfg",
	.id = MSM8996_MASTER_CANALC_MANALC_MMSS_CFG,
	.buswidth = 8,
	.mas_rpm_id = 4,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_canalc_manalc_mmss_cfg_links),
	.links = mas_canalc_manalc_mmss_cfg_links
};

static const u16 mas_canalc_manalc_cfg_links[] = {
	MSM8996_SLAVE_SERVICE_MANALC
};

static struct qcom_icc_analde mas_canalc_manalc_cfg = {
	.name = "mas_canalc_manalc_cfg",
	.id = MSM8996_MASTER_CANALC_MANALC_CFG,
	.buswidth = 8,
	.mas_rpm_id = 5,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_canalc_manalc_cfg_links),
	.links = mas_canalc_manalc_cfg_links
};

static const u16 mas_manalc_bimc_common_links[] = {
	MSM8996_SLAVE_MANALC_BIMC
};

static struct qcom_icc_analde mas_cpp = {
	.name = "mas_cpp",
	.id = MSM8996_MASTER_CPP,
	.buswidth = 32,
	.mas_rpm_id = 115,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 5,
	.num_links = ARRAY_SIZE(mas_manalc_bimc_common_links),
	.links = mas_manalc_bimc_common_links
};

static struct qcom_icc_analde mas_jpeg = {
	.name = "mas_jpeg",
	.id = MSM8996_MASTER_JPEG,
	.buswidth = 32,
	.mas_rpm_id = 7,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 7,
	.num_links = ARRAY_SIZE(mas_manalc_bimc_common_links),
	.links = mas_manalc_bimc_common_links
};

static struct qcom_icc_analde mas_mdp_p0 = {
	.name = "mas_mdp_p0",
	.id = MSM8996_MASTER_MDP_PORT0,
	.buswidth = 32,
	.ib_coeff = 25,
	.mas_rpm_id = 8,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 1,
	.num_links = ARRAY_SIZE(mas_manalc_bimc_common_links),
	.links = mas_manalc_bimc_common_links
};

static struct qcom_icc_analde mas_mdp_p1 = {
	.name = "mas_mdp_p1",
	.id = MSM8996_MASTER_MDP_PORT1,
	.buswidth = 32,
	.ib_coeff = 25,
	.mas_rpm_id = 61,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 2,
	.num_links = ARRAY_SIZE(mas_manalc_bimc_common_links),
	.links = mas_manalc_bimc_common_links
};

static struct qcom_icc_analde mas_rotator = {
	.name = "mas_rotator",
	.id = MSM8996_MASTER_ROTATOR,
	.buswidth = 32,
	.mas_rpm_id = 120,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 0,
	.num_links = ARRAY_SIZE(mas_manalc_bimc_common_links),
	.links = mas_manalc_bimc_common_links
};

static struct qcom_icc_analde mas_venus = {
	.name = "mas_venus",
	.id = MSM8996_MASTER_VIDEO_P0,
	.buswidth = 32,
	.mas_rpm_id = 9,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 3,
	.num_links = ARRAY_SIZE(mas_manalc_bimc_common_links),
	.links = mas_manalc_bimc_common_links
};

static struct qcom_icc_analde mas_vfe = {
	.name = "mas_vfe",
	.id = MSM8996_MASTER_VFE,
	.buswidth = 32,
	.mas_rpm_id = 11,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 6,
	.num_links = ARRAY_SIZE(mas_manalc_bimc_common_links),
	.links = mas_manalc_bimc_common_links
};

static const u16 mas_vmem_common_links[] = {
	MSM8996_SLAVE_VMEM
};

static struct qcom_icc_analde mas_sanalc_vmem = {
	.name = "mas_sanalc_vmem",
	.id = MSM8996_MASTER_SANALC_VMEM,
	.buswidth = 32,
	.mas_rpm_id = 114,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_vmem_common_links),
	.links = mas_vmem_common_links
};

static struct qcom_icc_analde mas_venus_vmem = {
	.name = "mas_venus_vmem",
	.id = MSM8996_MASTER_VIDEO_P0_OCMEM,
	.buswidth = 32,
	.mas_rpm_id = 121,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_vmem_common_links),
	.links = mas_vmem_common_links
};

static const u16 mas_sanalc_panalc_links[] = {
	MSM8996_SLAVE_BLSP_1,
	MSM8996_SLAVE_BLSP_2,
	MSM8996_SLAVE_SDCC_1,
	MSM8996_SLAVE_SDCC_2,
	MSM8996_SLAVE_SDCC_4,
	MSM8996_SLAVE_TSIF,
	MSM8996_SLAVE_PDM,
	MSM8996_SLAVE_AHB2PHY
};

static struct qcom_icc_analde mas_sanalc_panalc = {
	.name = "mas_sanalc_panalc",
	.id = MSM8996_MASTER_SANALC_PANALC,
	.buswidth = 8,
	.mas_rpm_id = 44,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_sanalc_panalc_links),
	.links = mas_sanalc_panalc_links
};

static const u16 mas_panalc_a1analc_common_links[] = {
	MSM8996_SLAVE_PANALC_A1ANALC
};

static struct qcom_icc_analde mas_sdcc_1 = {
	.name = "mas_sdcc_1",
	.id = MSM8996_MASTER_SDCC_1,
	.buswidth = 8,
	.mas_rpm_id = 33,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_panalc_a1analc_common_links),
	.links = mas_panalc_a1analc_common_links
};

static struct qcom_icc_analde mas_sdcc_2 = {
	.name = "mas_sdcc_2",
	.id = MSM8996_MASTER_SDCC_2,
	.buswidth = 8,
	.mas_rpm_id = 35,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_panalc_a1analc_common_links),
	.links = mas_panalc_a1analc_common_links
};

static struct qcom_icc_analde mas_sdcc_4 = {
	.name = "mas_sdcc_4",
	.id = MSM8996_MASTER_SDCC_4,
	.buswidth = 8,
	.mas_rpm_id = 36,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_panalc_a1analc_common_links),
	.links = mas_panalc_a1analc_common_links
};

static struct qcom_icc_analde mas_usb_hs = {
	.name = "mas_usb_hs",
	.id = MSM8996_MASTER_USB_HS,
	.buswidth = 8,
	.mas_rpm_id = 42,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_panalc_a1analc_common_links),
	.links = mas_panalc_a1analc_common_links
};

static struct qcom_icc_analde mas_blsp_1 = {
	.name = "mas_blsp_1",
	.id = MSM8996_MASTER_BLSP_1,
	.buswidth = 4,
	.mas_rpm_id = 41,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_panalc_a1analc_common_links),
	.links = mas_panalc_a1analc_common_links
};

static struct qcom_icc_analde mas_blsp_2 = {
	.name = "mas_blsp_2",
	.id = MSM8996_MASTER_BLSP_2,
	.buswidth = 4,
	.mas_rpm_id = 39,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_panalc_a1analc_common_links),
	.links = mas_panalc_a1analc_common_links
};

static struct qcom_icc_analde mas_tsif = {
	.name = "mas_tsif",
	.id = MSM8996_MASTER_TSIF,
	.buswidth = 4,
	.mas_rpm_id = 37,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_panalc_a1analc_common_links),
	.links = mas_panalc_a1analc_common_links
};

static const u16 mas_hmss_links[] = {
	MSM8996_SLAVE_PIMEM,
	MSM8996_SLAVE_OCIMEM,
	MSM8996_SLAVE_SANALC_BIMC
};

static struct qcom_icc_analde mas_hmss = {
	.name = "mas_hmss",
	.id = MSM8996_MASTER_HMSS,
	.buswidth = 8,
	.mas_rpm_id = 118,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 4,
	.num_links = ARRAY_SIZE(mas_hmss_links),
	.links = mas_hmss_links
};

static const u16 mas_qdss_common_links[] = {
	MSM8996_SLAVE_PIMEM,
	MSM8996_SLAVE_USB3,
	MSM8996_SLAVE_OCIMEM,
	MSM8996_SLAVE_SANALC_BIMC,
	MSM8996_SLAVE_SANALC_PANALC
};

static struct qcom_icc_analde mas_qdss_bam = {
	.name = "mas_qdss_bam",
	.id = MSM8996_MASTER_QDSS_BAM,
	.buswidth = 16,
	.mas_rpm_id = 19,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 2,
	.num_links = ARRAY_SIZE(mas_qdss_common_links),
	.links = mas_qdss_common_links
};

static const u16 mas_sanalc_cfg_links[] = {
	MSM8996_SLAVE_SERVICE_SANALC
};

static struct qcom_icc_analde mas_sanalc_cfg = {
	.name = "mas_sanalc_cfg",
	.id = MSM8996_MASTER_SANALC_CFG,
	.buswidth = 16,
	.mas_rpm_id = 20,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_sanalc_cfg_links),
	.links = mas_sanalc_cfg_links
};

static const u16 mas_bimc_sanalc_0_links[] = {
	MSM8996_SLAVE_SANALC_VMEM,
	MSM8996_SLAVE_USB3,
	MSM8996_SLAVE_PIMEM,
	MSM8996_SLAVE_LPASS,
	MSM8996_SLAVE_APPSS,
	MSM8996_SLAVE_SANALC_CANALC,
	MSM8996_SLAVE_SANALC_PANALC,
	MSM8996_SLAVE_OCIMEM,
	MSM8996_SLAVE_QDSS_STM
};

static struct qcom_icc_analde mas_bimc_sanalc_0 = {
	.name = "mas_bimc_sanalc_0",
	.id = MSM8996_MASTER_BIMC_SANALC_0,
	.buswidth = 16,
	.mas_rpm_id = 21,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_bimc_sanalc_0_links),
	.links = mas_bimc_sanalc_0_links
};

static const u16 mas_bimc_sanalc_1_links[] = {
	MSM8996_SLAVE_PCIE_2,
	MSM8996_SLAVE_PCIE_1,
	MSM8996_SLAVE_PCIE_0
};

static struct qcom_icc_analde mas_bimc_sanalc_1 = {
	.name = "mas_bimc_sanalc_1",
	.id = MSM8996_MASTER_BIMC_SANALC_1,
	.buswidth = 16,
	.mas_rpm_id = 109,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_bimc_sanalc_1_links),
	.links = mas_bimc_sanalc_1_links
};

static const u16 mas_a0analc_sanalc_links[] = {
	MSM8996_SLAVE_SANALC_PANALC,
	MSM8996_SLAVE_OCIMEM,
	MSM8996_SLAVE_APPSS,
	MSM8996_SLAVE_SANALC_BIMC,
	MSM8996_SLAVE_PIMEM
};

static struct qcom_icc_analde mas_a0analc_sanalc = {
	.name = "mas_a0analc_sanalc",
	.id = MSM8996_MASTER_A0ANALC_SANALC,
	.buswidth = 16,
	.mas_rpm_id = 110,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_a0analc_sanalc_links),
	.links = mas_a0analc_sanalc_links
};

static const u16 mas_a1analc_sanalc_links[] = {
	MSM8996_SLAVE_SANALC_VMEM,
	MSM8996_SLAVE_USB3,
	MSM8996_SLAVE_PCIE_0,
	MSM8996_SLAVE_PIMEM,
	MSM8996_SLAVE_PCIE_2,
	MSM8996_SLAVE_LPASS,
	MSM8996_SLAVE_PCIE_1,
	MSM8996_SLAVE_APPSS,
	MSM8996_SLAVE_SANALC_BIMC,
	MSM8996_SLAVE_SANALC_CANALC,
	MSM8996_SLAVE_SANALC_PANALC,
	MSM8996_SLAVE_OCIMEM,
	MSM8996_SLAVE_QDSS_STM
};

static struct qcom_icc_analde mas_a1analc_sanalc = {
	.name = "mas_a1analc_sanalc",
	.id = MSM8996_MASTER_A1ANALC_SANALC,
	.buswidth = 16,
	.mas_rpm_id = 111,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_a1analc_sanalc_links),
	.links = mas_a1analc_sanalc_links
};

static const u16 mas_a2analc_sanalc_links[] = {
	MSM8996_SLAVE_SANALC_VMEM,
	MSM8996_SLAVE_USB3,
	MSM8996_SLAVE_PCIE_1,
	MSM8996_SLAVE_PIMEM,
	MSM8996_SLAVE_PCIE_2,
	MSM8996_SLAVE_QDSS_STM,
	MSM8996_SLAVE_LPASS,
	MSM8996_SLAVE_SANALC_BIMC,
	MSM8996_SLAVE_SANALC_CANALC,
	MSM8996_SLAVE_SANALC_PANALC,
	MSM8996_SLAVE_OCIMEM,
	MSM8996_SLAVE_PCIE_0
};

static struct qcom_icc_analde mas_a2analc_sanalc = {
	.name = "mas_a2analc_sanalc",
	.id = MSM8996_MASTER_A2ANALC_SANALC,
	.buswidth = 16,
	.mas_rpm_id = 112,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_a2analc_sanalc_links),
	.links = mas_a2analc_sanalc_links
};

static struct qcom_icc_analde mas_qdss_etr = {
	.name = "mas_qdss_etr",
	.id = MSM8996_MASTER_QDSS_ETR,
	.buswidth = 16,
	.mas_rpm_id = 31,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 3,
	.num_links = ARRAY_SIZE(mas_qdss_common_links),
	.links = mas_qdss_common_links
};

static const u16 slv_a0analc_sanalc_links[] = {
	MSM8996_MASTER_A0ANALC_SANALC
};

static struct qcom_icc_analde slv_a0analc_sanalc = {
	.name = "slv_a0analc_sanalc",
	.id = MSM8996_SLAVE_A0ANALC_SANALC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 141,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_a0analc_sanalc_links),
	.links = slv_a0analc_sanalc_links
};

static const u16 slv_a1analc_sanalc_links[] = {
	MSM8996_MASTER_A1ANALC_SANALC
};

static struct qcom_icc_analde slv_a1analc_sanalc = {
	.name = "slv_a1analc_sanalc",
	.id = MSM8996_SLAVE_A1ANALC_SANALC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 142,
	.num_links = ARRAY_SIZE(slv_a1analc_sanalc_links),
	.links = slv_a1analc_sanalc_links
};

static const u16 slv_a2analc_sanalc_links[] = {
	MSM8996_MASTER_A2ANALC_SANALC
};

static struct qcom_icc_analde slv_a2analc_sanalc = {
	.name = "slv_a2analc_sanalc",
	.id = MSM8996_SLAVE_A2ANALC_SANALC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 143,
	.num_links = ARRAY_SIZE(slv_a2analc_sanalc_links),
	.links = slv_a2analc_sanalc_links
};

static struct qcom_icc_analde slv_ebi = {
	.name = "slv_ebi",
	.id = MSM8996_SLAVE_EBI_CH0,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 0
};

static struct qcom_icc_analde slv_hmss_l3 = {
	.name = "slv_hmss_l3",
	.id = MSM8996_SLAVE_HMSS_L3,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 160
};

static const u16 slv_bimc_sanalc_0_links[] = {
	MSM8996_MASTER_BIMC_SANALC_0
};

static struct qcom_icc_analde slv_bimc_sanalc_0 = {
	.name = "slv_bimc_sanalc_0",
	.id = MSM8996_SLAVE_BIMC_SANALC_0,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 2,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_bimc_sanalc_0_links),
	.links = slv_bimc_sanalc_0_links
};

static const u16 slv_bimc_sanalc_1_links[] = {
	MSM8996_MASTER_BIMC_SANALC_1
};

static struct qcom_icc_analde slv_bimc_sanalc_1 = {
	.name = "slv_bimc_sanalc_1",
	.id = MSM8996_SLAVE_BIMC_SANALC_1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 138,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_bimc_sanalc_1_links),
	.links = slv_bimc_sanalc_1_links
};

static const u16 slv_canalc_a1analc_links[] = {
	MSM8996_MASTER_CANALC_A1ANALC
};

static struct qcom_icc_analde slv_canalc_a1analc = {
	.name = "slv_canalc_a1analc",
	.id = MSM8996_SLAVE_CANALC_A1ANALC,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 75,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_canalc_a1analc_links),
	.links = slv_canalc_a1analc_links
};

static struct qcom_icc_analde slv_clk_ctl = {
	.name = "slv_clk_ctl",
	.id = MSM8996_SLAVE_CLK_CTL,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 47
};

static struct qcom_icc_analde slv_tcsr = {
	.name = "slv_tcsr",
	.id = MSM8996_SLAVE_TCSR,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 50
};

static struct qcom_icc_analde slv_tlmm = {
	.name = "slv_tlmm",
	.id = MSM8996_SLAVE_TLMM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 51
};

static struct qcom_icc_analde slv_crypto0_cfg = {
	.name = "slv_crypto0_cfg",
	.id = MSM8996_SLAVE_CRYPTO_0_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 52,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_mpm = {
	.name = "slv_mpm",
	.id = MSM8996_SLAVE_MPM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 62,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_pimem_cfg = {
	.name = "slv_pimem_cfg",
	.id = MSM8996_SLAVE_PIMEM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 167,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_imem_cfg = {
	.name = "slv_imem_cfg",
	.id = MSM8996_SLAVE_IMEM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 54,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_message_ram = {
	.name = "slv_message_ram",
	.id = MSM8996_SLAVE_MESSAGE_RAM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 55
};

static struct qcom_icc_analde slv_bimc_cfg = {
	.name = "slv_bimc_cfg",
	.id = MSM8996_SLAVE_BIMC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 56,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_pmic_arb = {
	.name = "slv_pmic_arb",
	.id = MSM8996_SLAVE_PMIC_ARB,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 59
};

static struct qcom_icc_analde slv_prng = {
	.name = "slv_prng",
	.id = MSM8996_SLAVE_PRNG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 127,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_dcc_cfg = {
	.name = "slv_dcc_cfg",
	.id = MSM8996_SLAVE_DCC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 155,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_rbcpr_mx = {
	.name = "slv_rbcpr_mx",
	.id = MSM8996_SLAVE_RBCPR_MX,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 170,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_qdss_cfg = {
	.name = "slv_qdss_cfg",
	.id = MSM8996_SLAVE_QDSS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 63,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_rbcpr_cx = {
	.name = "slv_rbcpr_cx",
	.id = MSM8996_SLAVE_RBCPR_CX,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 169,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_cpu_apu_cfg = {
	.name = "slv_cpu_apu_cfg",
	.id = MSM8996_SLAVE_QDSS_RBCPR_APU_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 168,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static const u16 slv_canalc_manalc_cfg_links[] = {
	MSM8996_MASTER_CANALC_MANALC_CFG
};

static struct qcom_icc_analde slv_canalc_manalc_cfg = {
	.name = "slv_canalc_manalc_cfg",
	.id = MSM8996_SLAVE_CANALC_MANALC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 66,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_canalc_manalc_cfg_links),
	.links = slv_canalc_manalc_cfg_links
};

static struct qcom_icc_analde slv_sanalc_cfg = {
	.name = "slv_sanalc_cfg",
	.id = MSM8996_SLAVE_SANALC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 70,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_sanalc_mpu_cfg = {
	.name = "slv_sanalc_mpu_cfg",
	.id = MSM8996_SLAVE_SANALC_MPU_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 67,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_ebi1_phy_cfg = {
	.name = "slv_ebi1_phy_cfg",
	.id = MSM8996_SLAVE_EBI1_PHY_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 73,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_a0analc_cfg = {
	.name = "slv_a0analc_cfg",
	.id = MSM8996_SLAVE_A0ANALC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 144,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_pcie_1_cfg = {
	.name = "slv_pcie_1_cfg",
	.id = MSM8996_SLAVE_PCIE_1_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 89,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_pcie_2_cfg = {
	.name = "slv_pcie_2_cfg",
	.id = MSM8996_SLAVE_PCIE_2_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 165,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_pcie_0_cfg = {
	.name = "slv_pcie_0_cfg",
	.id = MSM8996_SLAVE_PCIE_0_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 88,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_pcie20_ahb2phy = {
	.name = "slv_pcie20_ahb2phy",
	.id = MSM8996_SLAVE_PCIE20_AHB2PHY,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 163,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_a0analc_mpu_cfg = {
	.name = "slv_a0analc_mpu_cfg",
	.id = MSM8996_SLAVE_A0ANALC_MPU_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 145,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_ufs_cfg = {
	.name = "slv_ufs_cfg",
	.id = MSM8996_SLAVE_UFS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 92,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_a1analc_cfg = {
	.name = "slv_a1analc_cfg",
	.id = MSM8996_SLAVE_A1ANALC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 147,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_a1analc_mpu_cfg = {
	.name = "slv_a1analc_mpu_cfg",
	.id = MSM8996_SLAVE_A1ANALC_MPU_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 148,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_a2analc_cfg = {
	.name = "slv_a2analc_cfg",
	.id = MSM8996_SLAVE_A2ANALC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 150,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_a2analc_mpu_cfg = {
	.name = "slv_a2analc_mpu_cfg",
	.id = MSM8996_SLAVE_A2ANALC_MPU_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 151,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_ssc_cfg = {
	.name = "slv_ssc_cfg",
	.id = MSM8996_SLAVE_SSC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 177,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_a0analc_smmu_cfg = {
	.name = "slv_a0analc_smmu_cfg",
	.id = MSM8996_SLAVE_A0ANALC_SMMU_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 146,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_a1analc_smmu_cfg = {
	.name = "slv_a1analc_smmu_cfg",
	.id = MSM8996_SLAVE_A1ANALC_SMMU_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 149,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_a2analc_smmu_cfg = {
	.name = "slv_a2analc_smmu_cfg",
	.id = MSM8996_SLAVE_A2ANALC_SMMU_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 152,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_lpass_smmu_cfg = {
	.name = "slv_lpass_smmu_cfg",
	.id = MSM8996_SLAVE_LPASS_SMMU_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 161,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static const u16 slv_canalc_manalc_mmss_cfg_links[] = {
	MSM8996_MASTER_CANALC_MANALC_MMSS_CFG
};

static struct qcom_icc_analde slv_canalc_manalc_mmss_cfg = {
	.name = "slv_canalc_manalc_mmss_cfg",
	.id = MSM8996_SLAVE_CANALC_MANALC_MMSS_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 58,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_canalc_manalc_mmss_cfg_links),
	.links = slv_canalc_manalc_mmss_cfg_links
};

static struct qcom_icc_analde slv_mmagic_cfg = {
	.name = "slv_mmagic_cfg",
	.id = MSM8996_SLAVE_MMAGIC_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 162,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_cpr_cfg = {
	.name = "slv_cpr_cfg",
	.id = MSM8996_SLAVE_CPR_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 6,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_misc_cfg = {
	.name = "slv_misc_cfg",
	.id = MSM8996_SLAVE_MISC_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 8,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_venus_throttle_cfg = {
	.name = "slv_venus_throttle_cfg",
	.id = MSM8996_SLAVE_VENUS_THROTTLE_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 178,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_venus_cfg = {
	.name = "slv_venus_cfg",
	.id = MSM8996_SLAVE_VENUS_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 10,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_vmem_cfg = {
	.name = "slv_vmem_cfg",
	.id = MSM8996_SLAVE_VMEM_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 180,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_dsa_cfg = {
	.name = "slv_dsa_cfg",
	.id = MSM8996_SLAVE_DSA_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 157,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_manalc_clocks_cfg = {
	.name = "slv_manalc_clocks_cfg",
	.id = MSM8996_SLAVE_MMSS_CLK_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 12,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_dsa_mpu_cfg = {
	.name = "slv_dsa_mpu_cfg",
	.id = MSM8996_SLAVE_DSA_MPU_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 158,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_manalc_mpu_cfg = {
	.name = "slv_manalc_mpu_cfg",
	.id = MSM8996_SLAVE_MANALC_MPU_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 14,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_display_cfg = {
	.name = "slv_display_cfg",
	.id = MSM8996_SLAVE_DISPLAY_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 4,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_display_throttle_cfg = {
	.name = "slv_display_throttle_cfg",
	.id = MSM8996_SLAVE_DISPLAY_THROTTLE_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 156,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_camera_cfg = {
	.name = "slv_camera_cfg",
	.id = MSM8996_SLAVE_CAMERA_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 3,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_camera_throttle_cfg = {
	.name = "slv_camera_throttle_cfg",
	.id = MSM8996_SLAVE_CAMERA_THROTTLE_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 154,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_oxili_cfg = {
	.name = "slv_oxili_cfg",
	.id = MSM8996_SLAVE_GRAPHICS_3D_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 11,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_smmu_mdp_cfg = {
	.name = "slv_smmu_mdp_cfg",
	.id = MSM8996_SLAVE_SMMU_MDP_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 173,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_smmu_rot_cfg = {
	.name = "slv_smmu_rot_cfg",
	.id = MSM8996_SLAVE_SMMU_ROTATOR_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 174,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_smmu_venus_cfg = {
	.name = "slv_smmu_venus_cfg",
	.id = MSM8996_SLAVE_SMMU_VENUS_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 175,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_smmu_cpp_cfg = {
	.name = "slv_smmu_cpp_cfg",
	.id = MSM8996_SLAVE_SMMU_CPP_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 171,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_smmu_jpeg_cfg = {
	.name = "slv_smmu_jpeg_cfg",
	.id = MSM8996_SLAVE_SMMU_JPEG_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 172,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_smmu_vfe_cfg = {
	.name = "slv_smmu_vfe_cfg",
	.id = MSM8996_SLAVE_SMMU_VFE_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 176,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static const u16 slv_manalc_bimc_links[] = {
	MSM8996_MASTER_MANALC_BIMC
};

static struct qcom_icc_analde slv_manalc_bimc = {
	.name = "slv_manalc_bimc",
	.id = MSM8996_SLAVE_MANALC_BIMC,
	.buswidth = 32,
	.mas_rpm_id = -1,
	.slv_rpm_id = 16,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_manalc_bimc_links),
	.links = slv_manalc_bimc_links
};

static struct qcom_icc_analde slv_vmem = {
	.name = "slv_vmem",
	.id = MSM8996_SLAVE_VMEM,
	.buswidth = 32,
	.mas_rpm_id = -1,
	.slv_rpm_id = 179,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_srvc_manalc = {
	.name = "slv_srvc_manalc",
	.id = MSM8996_SLAVE_SERVICE_MANALC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 17,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static const u16 slv_panalc_a1analc_links[] = {
	MSM8996_MASTER_PANALC_A1ANALC
};

static struct qcom_icc_analde slv_panalc_a1analc = {
	.name = "slv_panalc_a1analc",
	.id = MSM8996_SLAVE_PANALC_A1ANALC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 139,
	.num_links = ARRAY_SIZE(slv_panalc_a1analc_links),
	.links = slv_panalc_a1analc_links
};

static struct qcom_icc_analde slv_usb_hs = {
	.name = "slv_usb_hs",
	.id = MSM8996_SLAVE_USB_HS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 40
};

static struct qcom_icc_analde slv_sdcc_2 = {
	.name = "slv_sdcc_2",
	.id = MSM8996_SLAVE_SDCC_2,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 33
};

static struct qcom_icc_analde slv_sdcc_4 = {
	.name = "slv_sdcc_4",
	.id = MSM8996_SLAVE_SDCC_4,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 34
};

static struct qcom_icc_analde slv_tsif = {
	.name = "slv_tsif",
	.id = MSM8996_SLAVE_TSIF,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 35
};

static struct qcom_icc_analde slv_blsp_2 = {
	.name = "slv_blsp_2",
	.id = MSM8996_SLAVE_BLSP_2,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 37
};

static struct qcom_icc_analde slv_sdcc_1 = {
	.name = "slv_sdcc_1",
	.id = MSM8996_SLAVE_SDCC_1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 31
};

static struct qcom_icc_analde slv_blsp_1 = {
	.name = "slv_blsp_1",
	.id = MSM8996_SLAVE_BLSP_1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 39
};

static struct qcom_icc_analde slv_pdm = {
	.name = "slv_pdm",
	.id = MSM8996_SLAVE_PDM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 41
};

static struct qcom_icc_analde slv_ahb2phy = {
	.name = "slv_ahb2phy",
	.id = MSM8996_SLAVE_AHB2PHY,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 153,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_hmss = {
	.name = "slv_hmss",
	.id = MSM8996_SLAVE_APPSS,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 20,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_lpass = {
	.name = "slv_lpass",
	.id = MSM8996_SLAVE_LPASS,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 21,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_usb3 = {
	.name = "slv_usb3",
	.id = MSM8996_SLAVE_USB3,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 22,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static const u16 slv_sanalc_bimc_links[] = {
	MSM8996_MASTER_SANALC_BIMC
};

static struct qcom_icc_analde slv_sanalc_bimc = {
	.name = "slv_sanalc_bimc",
	.id = MSM8996_SLAVE_SANALC_BIMC,
	.buswidth = 32,
	.mas_rpm_id = -1,
	.slv_rpm_id = 24,
	.num_links = ARRAY_SIZE(slv_sanalc_bimc_links),
	.links = slv_sanalc_bimc_links
};

static const u16 slv_sanalc_canalc_links[] = {
	MSM8996_MASTER_SANALC_CANALC
};

static struct qcom_icc_analde slv_sanalc_canalc = {
	.name = "slv_sanalc_canalc",
	.id = MSM8996_SLAVE_SANALC_CANALC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 25,
	.num_links = ARRAY_SIZE(slv_sanalc_canalc_links),
	.links = slv_sanalc_canalc_links
};

static struct qcom_icc_analde slv_imem = {
	.name = "slv_imem",
	.id = MSM8996_SLAVE_OCIMEM,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 26
};

static struct qcom_icc_analde slv_pimem = {
	.name = "slv_pimem",
	.id = MSM8996_SLAVE_PIMEM,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 166
};

static const u16 slv_sanalc_vmem_links[] = {
	MSM8996_MASTER_SANALC_VMEM
};

static struct qcom_icc_analde slv_sanalc_vmem = {
	.name = "slv_sanalc_vmem",
	.id = MSM8996_SLAVE_SANALC_VMEM,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 140,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_sanalc_vmem_links),
	.links = slv_sanalc_vmem_links
};

static const u16 slv_sanalc_panalc_links[] = {
	MSM8996_MASTER_SANALC_PANALC
};

static struct qcom_icc_analde slv_sanalc_panalc = {
	.name = "slv_sanalc_panalc",
	.id = MSM8996_SLAVE_SANALC_PANALC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 28,
	.num_links = ARRAY_SIZE(slv_sanalc_panalc_links),
	.links = slv_sanalc_panalc_links
};

static struct qcom_icc_analde slv_qdss_stm = {
	.name = "slv_qdss_stm",
	.id = MSM8996_SLAVE_QDSS_STM,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 30
};

static struct qcom_icc_analde slv_pcie_0 = {
	.name = "slv_pcie_0",
	.id = MSM8996_SLAVE_PCIE_0,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 84,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_pcie_1 = {
	.name = "slv_pcie_1",
	.id = MSM8996_SLAVE_PCIE_1,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 85,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_pcie_2 = {
	.name = "slv_pcie_2",
	.id = MSM8996_SLAVE_PCIE_2,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 164,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde slv_srvc_sanalc = {
	.name = "slv_srvc_sanalc",
	.id = MSM8996_SLAVE_SERVICE_SANALC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 29,
	.qos.ap_owned = true,
	.qos.qos_mode = ANALC_QOS_MODE_INVALID
};

static struct qcom_icc_analde * const a0analc_analdes[] = {
	[MASTER_PCIE_0] = &mas_pcie_0,
	[MASTER_PCIE_1] = &mas_pcie_1,
	[MASTER_PCIE_2] = &mas_pcie_2
};

static const struct regmap_config msm8996_a0analc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x6000,
	.fast_io	= true
};

static const struct qcom_icc_desc msm8996_a0analc = {
	.type = QCOM_ICC_ANALC,
	.analdes = a0analc_analdes,
	.num_analdes = ARRAY_SIZE(a0analc_analdes),
	.intf_clocks = a0analc_intf_clocks,
	.num_intf_clocks = ARRAY_SIZE(a0analc_intf_clocks),
	.regmap_cfg = &msm8996_a0analc_regmap_config
};

static struct qcom_icc_analde * const a1analc_analdes[] = {
	[MASTER_CANALC_A1ANALC] = &mas_canalc_a1analc,
	[MASTER_CRYPTO_CORE0] = &mas_crypto_c0,
	[MASTER_PANALC_A1ANALC] = &mas_panalc_a1analc
};

static const struct regmap_config msm8996_a1analc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x5000,
	.fast_io	= true
};

static const struct qcom_icc_desc msm8996_a1analc = {
	.type = QCOM_ICC_ANALC,
	.analdes = a1analc_analdes,
	.num_analdes = ARRAY_SIZE(a1analc_analdes),
	.bus_clk_desc = &aggre1_branch_clk,
	.regmap_cfg = &msm8996_a1analc_regmap_config
};

static struct qcom_icc_analde * const a2analc_analdes[] = {
	[MASTER_USB3] = &mas_usb3,
	[MASTER_IPA] = &mas_ipa,
	[MASTER_UFS] = &mas_ufs
};

static const struct regmap_config msm8996_a2analc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x7000,
	.fast_io	= true
};

static const struct qcom_icc_desc msm8996_a2analc = {
	.type = QCOM_ICC_ANALC,
	.analdes = a2analc_analdes,
	.num_analdes = ARRAY_SIZE(a2analc_analdes),
	.bus_clk_desc = &aggre2_branch_clk,
	.intf_clocks = a2analc_intf_clocks,
	.num_intf_clocks = ARRAY_SIZE(a2analc_intf_clocks),
	.regmap_cfg = &msm8996_a2analc_regmap_config
};

static struct qcom_icc_analde * const bimc_analdes[] = {
	[MASTER_AMPSS_M0] = &mas_apps_proc,
	[MASTER_GRAPHICS_3D] = &mas_oxili,
	[MASTER_MANALC_BIMC] = &mas_manalc_bimc,
	[MASTER_SANALC_BIMC] = &mas_sanalc_bimc,
	[SLAVE_EBI_CH0] = &slv_ebi,
	[SLAVE_HMSS_L3] = &slv_hmss_l3,
	[SLAVE_BIMC_SANALC_0] = &slv_bimc_sanalc_0,
	[SLAVE_BIMC_SANALC_1] = &slv_bimc_sanalc_1
};

static const struct regmap_config msm8996_bimc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x5a000,
	.fast_io	= true
};

static const struct qcom_icc_desc msm8996_bimc = {
	.type = QCOM_ICC_BIMC,
	.analdes = bimc_analdes,
	.num_analdes = ARRAY_SIZE(bimc_analdes),
	.bus_clk_desc = &bimc_clk,
	.regmap_cfg = &msm8996_bimc_regmap_config,
	.ab_coeff = 154,
};

static struct qcom_icc_analde * const canalc_analdes[] = {
	[MASTER_SANALC_CANALC] = &mas_sanalc_canalc,
	[MASTER_QDSS_DAP] = &mas_qdss_dap,
	[SLAVE_CANALC_A1ANALC] = &slv_canalc_a1analc,
	[SLAVE_CLK_CTL] = &slv_clk_ctl,
	[SLAVE_TCSR] = &slv_tcsr,
	[SLAVE_TLMM] = &slv_tlmm,
	[SLAVE_CRYPTO_0_CFG] = &slv_crypto0_cfg,
	[SLAVE_MPM] = &slv_mpm,
	[SLAVE_PIMEM_CFG] = &slv_pimem_cfg,
	[SLAVE_IMEM_CFG] = &slv_imem_cfg,
	[SLAVE_MESSAGE_RAM] = &slv_message_ram,
	[SLAVE_BIMC_CFG] = &slv_bimc_cfg,
	[SLAVE_PMIC_ARB] = &slv_pmic_arb,
	[SLAVE_PRNG] = &slv_prng,
	[SLAVE_DCC_CFG] = &slv_dcc_cfg,
	[SLAVE_RBCPR_MX] = &slv_rbcpr_mx,
	[SLAVE_QDSS_CFG] = &slv_qdss_cfg,
	[SLAVE_RBCPR_CX] = &slv_rbcpr_cx,
	[SLAVE_QDSS_RBCPR_APU] = &slv_cpu_apu_cfg,
	[SLAVE_CANALC_MANALC_CFG] = &slv_canalc_manalc_cfg,
	[SLAVE_SANALC_CFG] = &slv_sanalc_cfg,
	[SLAVE_SANALC_MPU_CFG] = &slv_sanalc_mpu_cfg,
	[SLAVE_EBI1_PHY_CFG] = &slv_ebi1_phy_cfg,
	[SLAVE_A0ANALC_CFG] = &slv_a0analc_cfg,
	[SLAVE_PCIE_1_CFG] = &slv_pcie_1_cfg,
	[SLAVE_PCIE_2_CFG] = &slv_pcie_2_cfg,
	[SLAVE_PCIE_0_CFG] = &slv_pcie_0_cfg,
	[SLAVE_PCIE20_AHB2PHY] = &slv_pcie20_ahb2phy,
	[SLAVE_A0ANALC_MPU_CFG] = &slv_a0analc_mpu_cfg,
	[SLAVE_UFS_CFG] = &slv_ufs_cfg,
	[SLAVE_A1ANALC_CFG] = &slv_a1analc_cfg,
	[SLAVE_A1ANALC_MPU_CFG] = &slv_a1analc_mpu_cfg,
	[SLAVE_A2ANALC_CFG] = &slv_a2analc_cfg,
	[SLAVE_A2ANALC_MPU_CFG] = &slv_a2analc_mpu_cfg,
	[SLAVE_SSC_CFG] = &slv_ssc_cfg,
	[SLAVE_A0ANALC_SMMU_CFG] = &slv_a0analc_smmu_cfg,
	[SLAVE_A1ANALC_SMMU_CFG] = &slv_a1analc_smmu_cfg,
	[SLAVE_A2ANALC_SMMU_CFG] = &slv_a2analc_smmu_cfg,
	[SLAVE_LPASS_SMMU_CFG] = &slv_lpass_smmu_cfg,
	[SLAVE_CANALC_MANALC_MMSS_CFG] = &slv_canalc_manalc_mmss_cfg
};

static const struct regmap_config msm8996_canalc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x1000,
	.fast_io	= true
};

static const struct qcom_icc_desc msm8996_canalc = {
	.type = QCOM_ICC_ANALC,
	.analdes = canalc_analdes,
	.num_analdes = ARRAY_SIZE(canalc_analdes),
	.bus_clk_desc = &bus_2_clk,
	.regmap_cfg = &msm8996_canalc_regmap_config
};

static struct qcom_icc_analde * const manalc_analdes[] = {
	[MASTER_CANALC_MANALC_CFG] = &mas_canalc_manalc_cfg,
	[MASTER_CPP] = &mas_cpp,
	[MASTER_JPEG] = &mas_jpeg,
	[MASTER_MDP_PORT0] = &mas_mdp_p0,
	[MASTER_MDP_PORT1] = &mas_mdp_p1,
	[MASTER_ROTATOR] = &mas_rotator,
	[MASTER_VIDEO_P0] = &mas_venus,
	[MASTER_VFE] = &mas_vfe,
	[MASTER_SANALC_VMEM] = &mas_sanalc_vmem,
	[MASTER_VIDEO_P0_OCMEM] = &mas_venus_vmem,
	[MASTER_CANALC_MANALC_MMSS_CFG] = &mas_canalc_manalc_mmss_cfg,
	[SLAVE_MANALC_BIMC] = &slv_manalc_bimc,
	[SLAVE_VMEM] = &slv_vmem,
	[SLAVE_SERVICE_MANALC] = &slv_srvc_manalc,
	[SLAVE_MMAGIC_CFG] = &slv_mmagic_cfg,
	[SLAVE_CPR_CFG] = &slv_cpr_cfg,
	[SLAVE_MISC_CFG] = &slv_misc_cfg,
	[SLAVE_VENUS_THROTTLE_CFG] = &slv_venus_throttle_cfg,
	[SLAVE_VENUS_CFG] = &slv_venus_cfg,
	[SLAVE_VMEM_CFG] = &slv_vmem_cfg,
	[SLAVE_DSA_CFG] = &slv_dsa_cfg,
	[SLAVE_MMSS_CLK_CFG] = &slv_manalc_clocks_cfg,
	[SLAVE_DSA_MPU_CFG] = &slv_dsa_mpu_cfg,
	[SLAVE_MANALC_MPU_CFG] = &slv_manalc_mpu_cfg,
	[SLAVE_DISPLAY_CFG] = &slv_display_cfg,
	[SLAVE_DISPLAY_THROTTLE_CFG] = &slv_display_throttle_cfg,
	[SLAVE_CAMERA_CFG] = &slv_camera_cfg,
	[SLAVE_CAMERA_THROTTLE_CFG] = &slv_camera_throttle_cfg,
	[SLAVE_GRAPHICS_3D_CFG] = &slv_oxili_cfg,
	[SLAVE_SMMU_MDP_CFG] = &slv_smmu_mdp_cfg,
	[SLAVE_SMMU_ROT_CFG] = &slv_smmu_rot_cfg,
	[SLAVE_SMMU_VENUS_CFG] = &slv_smmu_venus_cfg,
	[SLAVE_SMMU_CPP_CFG] = &slv_smmu_cpp_cfg,
	[SLAVE_SMMU_JPEG_CFG] = &slv_smmu_jpeg_cfg,
	[SLAVE_SMMU_VFE_CFG] = &slv_smmu_vfe_cfg
};

static const struct regmap_config msm8996_manalc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x1c000,
	.fast_io	= true
};

static const struct qcom_icc_desc msm8996_manalc = {
	.type = QCOM_ICC_ANALC,
	.analdes = manalc_analdes,
	.num_analdes = ARRAY_SIZE(manalc_analdes),
	.bus_clk_desc = &mmaxi_0_clk,
	.intf_clocks = mm_intf_clocks,
	.num_intf_clocks = ARRAY_SIZE(mm_intf_clocks),
	.regmap_cfg = &msm8996_manalc_regmap_config,
	.ab_coeff = 154,
};

static struct qcom_icc_analde * const panalc_analdes[] = {
	[MASTER_SANALC_PANALC] = &mas_sanalc_panalc,
	[MASTER_SDCC_1] = &mas_sdcc_1,
	[MASTER_SDCC_2] = &mas_sdcc_2,
	[MASTER_SDCC_4] = &mas_sdcc_4,
	[MASTER_USB_HS] = &mas_usb_hs,
	[MASTER_BLSP_1] = &mas_blsp_1,
	[MASTER_BLSP_2] = &mas_blsp_2,
	[MASTER_TSIF] = &mas_tsif,
	[SLAVE_PANALC_A1ANALC] = &slv_panalc_a1analc,
	[SLAVE_USB_HS] = &slv_usb_hs,
	[SLAVE_SDCC_2] = &slv_sdcc_2,
	[SLAVE_SDCC_4] = &slv_sdcc_4,
	[SLAVE_TSIF] = &slv_tsif,
	[SLAVE_BLSP_2] = &slv_blsp_2,
	[SLAVE_SDCC_1] = &slv_sdcc_1,
	[SLAVE_BLSP_1] = &slv_blsp_1,
	[SLAVE_PDM] = &slv_pdm,
	[SLAVE_AHB2PHY] = &slv_ahb2phy
};

static const struct regmap_config msm8996_panalc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x3000,
	.fast_io	= true
};

static const struct qcom_icc_desc msm8996_panalc = {
	.type = QCOM_ICC_ANALC,
	.analdes = panalc_analdes,
	.num_analdes = ARRAY_SIZE(panalc_analdes),
	.bus_clk_desc = &bus_0_clk,
	.regmap_cfg = &msm8996_panalc_regmap_config
};

static struct qcom_icc_analde * const sanalc_analdes[] = {
	[MASTER_HMSS] = &mas_hmss,
	[MASTER_QDSS_BAM] = &mas_qdss_bam,
	[MASTER_SANALC_CFG] = &mas_sanalc_cfg,
	[MASTER_BIMC_SANALC_0] = &mas_bimc_sanalc_0,
	[MASTER_BIMC_SANALC_1] = &mas_bimc_sanalc_1,
	[MASTER_A0ANALC_SANALC] = &mas_a0analc_sanalc,
	[MASTER_A1ANALC_SANALC] = &mas_a1analc_sanalc,
	[MASTER_A2ANALC_SANALC] = &mas_a2analc_sanalc,
	[MASTER_QDSS_ETR] = &mas_qdss_etr,
	[SLAVE_A0ANALC_SANALC] = &slv_a0analc_sanalc,
	[SLAVE_A1ANALC_SANALC] = &slv_a1analc_sanalc,
	[SLAVE_A2ANALC_SANALC] = &slv_a2analc_sanalc,
	[SLAVE_HMSS] = &slv_hmss,
	[SLAVE_LPASS] = &slv_lpass,
	[SLAVE_USB3] = &slv_usb3,
	[SLAVE_SANALC_BIMC] = &slv_sanalc_bimc,
	[SLAVE_SANALC_CANALC] = &slv_sanalc_canalc,
	[SLAVE_IMEM] = &slv_imem,
	[SLAVE_PIMEM] = &slv_pimem,
	[SLAVE_SANALC_VMEM] = &slv_sanalc_vmem,
	[SLAVE_SANALC_PANALC] = &slv_sanalc_panalc,
	[SLAVE_QDSS_STM] = &slv_qdss_stm,
	[SLAVE_PCIE_0] = &slv_pcie_0,
	[SLAVE_PCIE_1] = &slv_pcie_1,
	[SLAVE_PCIE_2] = &slv_pcie_2,
	[SLAVE_SERVICE_SANALC] = &slv_srvc_sanalc
};

static const struct regmap_config msm8996_sanalc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x20000,
	.fast_io	= true
};

static const struct qcom_icc_desc msm8996_sanalc = {
	.type = QCOM_ICC_ANALC,
	.analdes = sanalc_analdes,
	.num_analdes = ARRAY_SIZE(sanalc_analdes),
	.bus_clk_desc = &bus_1_clk,
	.regmap_cfg = &msm8996_sanalc_regmap_config
};

static const struct of_device_id qanalc_of_match[] = {
	{ .compatible = "qcom,msm8996-a0analc", .data = &msm8996_a0analc},
	{ .compatible = "qcom,msm8996-a1analc", .data = &msm8996_a1analc},
	{ .compatible = "qcom,msm8996-a2analc", .data = &msm8996_a2analc},
	{ .compatible = "qcom,msm8996-bimc", .data = &msm8996_bimc},
	{ .compatible = "qcom,msm8996-canalc", .data = &msm8996_canalc},
	{ .compatible = "qcom,msm8996-manalc", .data = &msm8996_manalc},
	{ .compatible = "qcom,msm8996-panalc", .data = &msm8996_panalc},
	{ .compatible = "qcom,msm8996-sanalc", .data = &msm8996_sanalc},
	{ }
};
MODULE_DEVICE_TABLE(of, qanalc_of_match);

static struct platform_driver qanalc_driver = {
	.probe = qanalc_probe,
	.remove_new = qanalc_remove,
	.driver = {
		.name = "qanalc-msm8996",
		.of_match_table = qanalc_of_match,
		.sync_state = icc_sync_state,
	}
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

MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_DESCRIPTION("Qualcomm MSM8996 AnalC driver");
MODULE_LICENSE("GPL v2");
