// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm MSM8996 Network-on-Chip (NoC) QoS driver
 *
 * Copyright (c) 2021 Yassine Oudjana <y.oudjana@protonmail.com>
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/interconnect/qcom,msm8996.h>

#include "icc-rpm.h"
#include "smd-rpm.h"
#include "msm8996.h"

static const char * const bus_mm_clocks[] = {
	"bus",
	"bus_a",
	"iface"
};

static const char * const bus_a0noc_clocks[] = {
	"aggre0_snoc_axi",
	"aggre0_cnoc_ahb",
	"aggre0_noc_mpu_cfg"
};

static const u16 mas_a0noc_common_links[] = {
	MSM8996_SLAVE_A0NOC_SNOC
};

static struct qcom_icc_node mas_pcie_0 = {
	.name = "mas_pcie_0",
	.id = MSM8996_MASTER_PCIE_0,
	.buswidth = 8,
	.mas_rpm_id = 65,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 0,
	.num_links = ARRAY_SIZE(mas_a0noc_common_links),
	.links = mas_a0noc_common_links
};

static struct qcom_icc_node mas_pcie_1 = {
	.name = "mas_pcie_1",
	.id = MSM8996_MASTER_PCIE_1,
	.buswidth = 8,
	.mas_rpm_id = 66,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 1,
	.num_links = ARRAY_SIZE(mas_a0noc_common_links),
	.links = mas_a0noc_common_links
};

static struct qcom_icc_node mas_pcie_2 = {
	.name = "mas_pcie_2",
	.id = MSM8996_MASTER_PCIE_2,
	.buswidth = 8,
	.mas_rpm_id = 119,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 2,
	.num_links = ARRAY_SIZE(mas_a0noc_common_links),
	.links = mas_a0noc_common_links
};

static const u16 mas_a1noc_common_links[] = {
	MSM8996_SLAVE_A1NOC_SNOC
};

static struct qcom_icc_node mas_cnoc_a1noc = {
	.name = "mas_cnoc_a1noc",
	.id = MSM8996_MASTER_CNOC_A1NOC,
	.buswidth = 8,
	.mas_rpm_id = 116,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_a1noc_common_links),
	.links = mas_a1noc_common_links
};

static struct qcom_icc_node mas_crypto_c0 = {
	.name = "mas_crypto_c0",
	.id = MSM8996_MASTER_CRYPTO_CORE0,
	.buswidth = 8,
	.mas_rpm_id = 23,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 0,
	.num_links = ARRAY_SIZE(mas_a1noc_common_links),
	.links = mas_a1noc_common_links
};

static struct qcom_icc_node mas_pnoc_a1noc = {
	.name = "mas_pnoc_a1noc",
	.id = MSM8996_MASTER_PNOC_A1NOC,
	.buswidth = 8,
	.mas_rpm_id = 117,
	.slv_rpm_id = -1,
	.qos.ap_owned = false,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 1,
	.num_links = ARRAY_SIZE(mas_a1noc_common_links),
	.links = mas_a1noc_common_links
};

static const u16 mas_a2noc_common_links[] = {
	MSM8996_SLAVE_A2NOC_SNOC
};

static struct qcom_icc_node mas_usb3 = {
	.name = "mas_usb3",
	.id = MSM8996_MASTER_USB3,
	.buswidth = 8,
	.mas_rpm_id = 32,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 3,
	.num_links = ARRAY_SIZE(mas_a2noc_common_links),
	.links = mas_a2noc_common_links
};

static struct qcom_icc_node mas_ipa = {
	.name = "mas_ipa",
	.id = MSM8996_MASTER_IPA,
	.buswidth = 8,
	.mas_rpm_id = 59,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = -1,
	.num_links = ARRAY_SIZE(mas_a2noc_common_links),
	.links = mas_a2noc_common_links
};

static struct qcom_icc_node mas_ufs = {
	.name = "mas_ufs",
	.id = MSM8996_MASTER_UFS,
	.buswidth = 8,
	.mas_rpm_id = 68,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 2,
	.num_links = ARRAY_SIZE(mas_a2noc_common_links),
	.links = mas_a2noc_common_links
};

static const u16 mas_apps_proc_links[] = {
	MSM8996_SLAVE_BIMC_SNOC_1,
	MSM8996_SLAVE_EBI_CH0,
	MSM8996_SLAVE_BIMC_SNOC_0
};

static struct qcom_icc_node mas_apps_proc = {
	.name = "mas_apps_proc",
	.id = MSM8996_MASTER_AMPSS_M0,
	.buswidth = 8,
	.mas_rpm_id = 0,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 0,
	.num_links = ARRAY_SIZE(mas_apps_proc_links),
	.links = mas_apps_proc_links
};

static const u16 mas_oxili_common_links[] = {
	MSM8996_SLAVE_BIMC_SNOC_1,
	MSM8996_SLAVE_HMSS_L3,
	MSM8996_SLAVE_EBI_CH0,
	MSM8996_SLAVE_BIMC_SNOC_0
};

static struct qcom_icc_node mas_oxili = {
	.name = "mas_oxili",
	.id = MSM8996_MASTER_GRAPHICS_3D,
	.buswidth = 8,
	.mas_rpm_id = 6,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 1,
	.num_links = ARRAY_SIZE(mas_oxili_common_links),
	.links = mas_oxili_common_links
};

static struct qcom_icc_node mas_mnoc_bimc = {
	.name = "mas_mnoc_bimc",
	.id = MSM8996_MASTER_MNOC_BIMC,
	.buswidth = 8,
	.mas_rpm_id = 2,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 2,
	.num_links = ARRAY_SIZE(mas_oxili_common_links),
	.links = mas_oxili_common_links
};

static const u16 mas_snoc_bimc_links[] = {
	MSM8996_SLAVE_HMSS_L3,
	MSM8996_SLAVE_EBI_CH0
};

static struct qcom_icc_node mas_snoc_bimc = {
	.name = "mas_snoc_bimc",
	.id = MSM8996_MASTER_SNOC_BIMC,
	.buswidth = 8,
	.mas_rpm_id = 3,
	.slv_rpm_id = -1,
	.qos.ap_owned = false,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = -1,
	.num_links = ARRAY_SIZE(mas_snoc_bimc_links),
	.links = mas_snoc_bimc_links
};

static const u16 mas_snoc_cnoc_links[] = {
	MSM8996_SLAVE_CLK_CTL,
	MSM8996_SLAVE_RBCPR_CX,
	MSM8996_SLAVE_A2NOC_SMMU_CFG,
	MSM8996_SLAVE_A0NOC_MPU_CFG,
	MSM8996_SLAVE_MESSAGE_RAM,
	MSM8996_SLAVE_CNOC_MNOC_MMSS_CFG,
	MSM8996_SLAVE_PCIE_0_CFG,
	MSM8996_SLAVE_TLMM,
	MSM8996_SLAVE_MPM,
	MSM8996_SLAVE_A0NOC_SMMU_CFG,
	MSM8996_SLAVE_EBI1_PHY_CFG,
	MSM8996_SLAVE_BIMC_CFG,
	MSM8996_SLAVE_PIMEM_CFG,
	MSM8996_SLAVE_RBCPR_MX,
	MSM8996_SLAVE_PRNG,
	MSM8996_SLAVE_PCIE20_AHB2PHY,
	MSM8996_SLAVE_A2NOC_MPU_CFG,
	MSM8996_SLAVE_QDSS_CFG,
	MSM8996_SLAVE_A2NOC_CFG,
	MSM8996_SLAVE_A0NOC_CFG,
	MSM8996_SLAVE_UFS_CFG,
	MSM8996_SLAVE_CRYPTO_0_CFG,
	MSM8996_SLAVE_PCIE_1_CFG,
	MSM8996_SLAVE_SNOC_CFG,
	MSM8996_SLAVE_SNOC_MPU_CFG,
	MSM8996_SLAVE_A1NOC_MPU_CFG,
	MSM8996_SLAVE_A1NOC_SMMU_CFG,
	MSM8996_SLAVE_PCIE_2_CFG,
	MSM8996_SLAVE_CNOC_MNOC_CFG,
	MSM8996_SLAVE_QDSS_RBCPR_APU_CFG,
	MSM8996_SLAVE_PMIC_ARB,
	MSM8996_SLAVE_IMEM_CFG,
	MSM8996_SLAVE_A1NOC_CFG,
	MSM8996_SLAVE_SSC_CFG,
	MSM8996_SLAVE_TCSR,
	MSM8996_SLAVE_LPASS_SMMU_CFG,
	MSM8996_SLAVE_DCC_CFG
};

static struct qcom_icc_node mas_snoc_cnoc = {
	.name = "mas_snoc_cnoc",
	.id = MSM8996_MASTER_SNOC_CNOC,
	.buswidth = 8,
	.mas_rpm_id = 52,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_snoc_cnoc_links),
	.links = mas_snoc_cnoc_links
};

static const u16 mas_qdss_dap_links[] = {
	MSM8996_SLAVE_QDSS_RBCPR_APU_CFG,
	MSM8996_SLAVE_RBCPR_CX,
	MSM8996_SLAVE_A2NOC_SMMU_CFG,
	MSM8996_SLAVE_A0NOC_MPU_CFG,
	MSM8996_SLAVE_MESSAGE_RAM,
	MSM8996_SLAVE_PCIE_0_CFG,
	MSM8996_SLAVE_TLMM,
	MSM8996_SLAVE_MPM,
	MSM8996_SLAVE_A0NOC_SMMU_CFG,
	MSM8996_SLAVE_EBI1_PHY_CFG,
	MSM8996_SLAVE_BIMC_CFG,
	MSM8996_SLAVE_PIMEM_CFG,
	MSM8996_SLAVE_RBCPR_MX,
	MSM8996_SLAVE_CLK_CTL,
	MSM8996_SLAVE_PRNG,
	MSM8996_SLAVE_PCIE20_AHB2PHY,
	MSM8996_SLAVE_A2NOC_MPU_CFG,
	MSM8996_SLAVE_QDSS_CFG,
	MSM8996_SLAVE_A2NOC_CFG,
	MSM8996_SLAVE_A0NOC_CFG,
	MSM8996_SLAVE_UFS_CFG,
	MSM8996_SLAVE_CRYPTO_0_CFG,
	MSM8996_SLAVE_CNOC_A1NOC,
	MSM8996_SLAVE_PCIE_1_CFG,
	MSM8996_SLAVE_SNOC_CFG,
	MSM8996_SLAVE_SNOC_MPU_CFG,
	MSM8996_SLAVE_A1NOC_MPU_CFG,
	MSM8996_SLAVE_A1NOC_SMMU_CFG,
	MSM8996_SLAVE_PCIE_2_CFG,
	MSM8996_SLAVE_CNOC_MNOC_CFG,
	MSM8996_SLAVE_CNOC_MNOC_MMSS_CFG,
	MSM8996_SLAVE_PMIC_ARB,
	MSM8996_SLAVE_IMEM_CFG,
	MSM8996_SLAVE_A1NOC_CFG,
	MSM8996_SLAVE_SSC_CFG,
	MSM8996_SLAVE_TCSR,
	MSM8996_SLAVE_LPASS_SMMU_CFG,
	MSM8996_SLAVE_DCC_CFG
};

static struct qcom_icc_node mas_qdss_dap = {
	.name = "mas_qdss_dap",
	.id = MSM8996_MASTER_QDSS_DAP,
	.buswidth = 8,
	.mas_rpm_id = 49,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_qdss_dap_links),
	.links = mas_qdss_dap_links
};

static const u16 mas_cnoc_mnoc_mmss_cfg_links[] = {
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
	MSM8996_SLAVE_MNOC_MPU_CFG
};

static struct qcom_icc_node mas_cnoc_mnoc_mmss_cfg = {
	.name = "mas_cnoc_mnoc_mmss_cfg",
	.id = MSM8996_MASTER_CNOC_MNOC_MMSS_CFG,
	.buswidth = 8,
	.mas_rpm_id = 4,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_cnoc_mnoc_mmss_cfg_links),
	.links = mas_cnoc_mnoc_mmss_cfg_links
};

static const u16 mas_cnoc_mnoc_cfg_links[] = {
	MSM8996_SLAVE_SERVICE_MNOC
};

static struct qcom_icc_node mas_cnoc_mnoc_cfg = {
	.name = "mas_cnoc_mnoc_cfg",
	.id = MSM8996_MASTER_CNOC_MNOC_CFG,
	.buswidth = 8,
	.mas_rpm_id = 5,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_cnoc_mnoc_cfg_links),
	.links = mas_cnoc_mnoc_cfg_links
};

static const u16 mas_mnoc_bimc_common_links[] = {
	MSM8996_SLAVE_MNOC_BIMC
};

static struct qcom_icc_node mas_cpp = {
	.name = "mas_cpp",
	.id = MSM8996_MASTER_CPP,
	.buswidth = 32,
	.mas_rpm_id = 115,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 5,
	.num_links = ARRAY_SIZE(mas_mnoc_bimc_common_links),
	.links = mas_mnoc_bimc_common_links
};

static struct qcom_icc_node mas_jpeg = {
	.name = "mas_jpeg",
	.id = MSM8996_MASTER_JPEG,
	.buswidth = 32,
	.mas_rpm_id = 7,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 7,
	.num_links = ARRAY_SIZE(mas_mnoc_bimc_common_links),
	.links = mas_mnoc_bimc_common_links
};

static struct qcom_icc_node mas_mdp_p0 = {
	.name = "mas_mdp_p0",
	.id = MSM8996_MASTER_MDP_PORT0,
	.buswidth = 32,
	.mas_rpm_id = 8,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 1,
	.num_links = ARRAY_SIZE(mas_mnoc_bimc_common_links),
	.links = mas_mnoc_bimc_common_links
};

static struct qcom_icc_node mas_mdp_p1 = {
	.name = "mas_mdp_p1",
	.id = MSM8996_MASTER_MDP_PORT1,
	.buswidth = 32,
	.mas_rpm_id = 61,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 2,
	.num_links = ARRAY_SIZE(mas_mnoc_bimc_common_links),
	.links = mas_mnoc_bimc_common_links
};

static struct qcom_icc_node mas_rotator = {
	.name = "mas_rotator",
	.id = MSM8996_MASTER_ROTATOR,
	.buswidth = 32,
	.mas_rpm_id = 120,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 0,
	.num_links = ARRAY_SIZE(mas_mnoc_bimc_common_links),
	.links = mas_mnoc_bimc_common_links
};

static struct qcom_icc_node mas_venus = {
	.name = "mas_venus",
	.id = MSM8996_MASTER_VIDEO_P0,
	.buswidth = 32,
	.mas_rpm_id = 9,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 3,
	.num_links = ARRAY_SIZE(mas_mnoc_bimc_common_links),
	.links = mas_mnoc_bimc_common_links
};

static struct qcom_icc_node mas_vfe = {
	.name = "mas_vfe",
	.id = MSM8996_MASTER_VFE,
	.buswidth = 32,
	.mas_rpm_id = 11,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 6,
	.num_links = ARRAY_SIZE(mas_mnoc_bimc_common_links),
	.links = mas_mnoc_bimc_common_links
};

static const u16 mas_vmem_common_links[] = {
	MSM8996_SLAVE_VMEM
};

static struct qcom_icc_node mas_snoc_vmem = {
	.name = "mas_snoc_vmem",
	.id = MSM8996_MASTER_SNOC_VMEM,
	.buswidth = 32,
	.mas_rpm_id = 114,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_vmem_common_links),
	.links = mas_vmem_common_links
};

static struct qcom_icc_node mas_venus_vmem = {
	.name = "mas_venus_vmem",
	.id = MSM8996_MASTER_VIDEO_P0_OCMEM,
	.buswidth = 32,
	.mas_rpm_id = 121,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_vmem_common_links),
	.links = mas_vmem_common_links
};

static const u16 mas_snoc_pnoc_links[] = {
	MSM8996_SLAVE_BLSP_1,
	MSM8996_SLAVE_BLSP_2,
	MSM8996_SLAVE_SDCC_1,
	MSM8996_SLAVE_SDCC_2,
	MSM8996_SLAVE_SDCC_4,
	MSM8996_SLAVE_TSIF,
	MSM8996_SLAVE_PDM,
	MSM8996_SLAVE_AHB2PHY
};

static struct qcom_icc_node mas_snoc_pnoc = {
	.name = "mas_snoc_pnoc",
	.id = MSM8996_MASTER_SNOC_PNOC,
	.buswidth = 8,
	.mas_rpm_id = 44,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_snoc_pnoc_links),
	.links = mas_snoc_pnoc_links
};

static const u16 mas_pnoc_a1noc_common_links[] = {
	MSM8996_SLAVE_PNOC_A1NOC
};

static struct qcom_icc_node mas_sdcc_1 = {
	.name = "mas_sdcc_1",
	.id = MSM8996_MASTER_SDCC_1,
	.buswidth = 8,
	.mas_rpm_id = 33,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_pnoc_a1noc_common_links),
	.links = mas_pnoc_a1noc_common_links
};

static struct qcom_icc_node mas_sdcc_2 = {
	.name = "mas_sdcc_2",
	.id = MSM8996_MASTER_SDCC_2,
	.buswidth = 8,
	.mas_rpm_id = 35,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_pnoc_a1noc_common_links),
	.links = mas_pnoc_a1noc_common_links
};

static struct qcom_icc_node mas_sdcc_4 = {
	.name = "mas_sdcc_4",
	.id = MSM8996_MASTER_SDCC_4,
	.buswidth = 8,
	.mas_rpm_id = 36,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_pnoc_a1noc_common_links),
	.links = mas_pnoc_a1noc_common_links
};

static struct qcom_icc_node mas_usb_hs = {
	.name = "mas_usb_hs",
	.id = MSM8996_MASTER_USB_HS,
	.buswidth = 8,
	.mas_rpm_id = 42,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_pnoc_a1noc_common_links),
	.links = mas_pnoc_a1noc_common_links
};

static struct qcom_icc_node mas_blsp_1 = {
	.name = "mas_blsp_1",
	.id = MSM8996_MASTER_BLSP_1,
	.buswidth = 4,
	.mas_rpm_id = 41,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_pnoc_a1noc_common_links),
	.links = mas_pnoc_a1noc_common_links
};

static struct qcom_icc_node mas_blsp_2 = {
	.name = "mas_blsp_2",
	.id = MSM8996_MASTER_BLSP_2,
	.buswidth = 4,
	.mas_rpm_id = 39,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_pnoc_a1noc_common_links),
	.links = mas_pnoc_a1noc_common_links
};

static struct qcom_icc_node mas_tsif = {
	.name = "mas_tsif",
	.id = MSM8996_MASTER_TSIF,
	.buswidth = 4,
	.mas_rpm_id = 37,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_pnoc_a1noc_common_links),
	.links = mas_pnoc_a1noc_common_links
};

static const u16 mas_hmss_links[] = {
	MSM8996_SLAVE_PIMEM,
	MSM8996_SLAVE_OCIMEM,
	MSM8996_SLAVE_SNOC_BIMC
};

static struct qcom_icc_node mas_hmss = {
	.name = "mas_hmss",
	.id = MSM8996_MASTER_HMSS,
	.buswidth = 8,
	.mas_rpm_id = 118,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
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
	MSM8996_SLAVE_SNOC_BIMC,
	MSM8996_SLAVE_SNOC_PNOC
};

static struct qcom_icc_node mas_qdss_bam = {
	.name = "mas_qdss_bam",
	.id = MSM8996_MASTER_QDSS_BAM,
	.buswidth = 16,
	.mas_rpm_id = 19,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 2,
	.num_links = ARRAY_SIZE(mas_qdss_common_links),
	.links = mas_qdss_common_links
};

static const u16 mas_snoc_cfg_links[] = {
	MSM8996_SLAVE_SERVICE_SNOC
};

static struct qcom_icc_node mas_snoc_cfg = {
	.name = "mas_snoc_cfg",
	.id = MSM8996_MASTER_SNOC_CFG,
	.buswidth = 16,
	.mas_rpm_id = 20,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_snoc_cfg_links),
	.links = mas_snoc_cfg_links
};

static const u16 mas_bimc_snoc_0_links[] = {
	MSM8996_SLAVE_SNOC_VMEM,
	MSM8996_SLAVE_USB3,
	MSM8996_SLAVE_PIMEM,
	MSM8996_SLAVE_LPASS,
	MSM8996_SLAVE_APPSS,
	MSM8996_SLAVE_SNOC_CNOC,
	MSM8996_SLAVE_SNOC_PNOC,
	MSM8996_SLAVE_OCIMEM,
	MSM8996_SLAVE_QDSS_STM
};

static struct qcom_icc_node mas_bimc_snoc_0 = {
	.name = "mas_bimc_snoc_0",
	.id = MSM8996_MASTER_BIMC_SNOC_0,
	.buswidth = 16,
	.mas_rpm_id = 21,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_bimc_snoc_0_links),
	.links = mas_bimc_snoc_0_links
};

static const u16 mas_bimc_snoc_1_links[] = {
	MSM8996_SLAVE_PCIE_2,
	MSM8996_SLAVE_PCIE_1,
	MSM8996_SLAVE_PCIE_0
};

static struct qcom_icc_node mas_bimc_snoc_1 = {
	.name = "mas_bimc_snoc_1",
	.id = MSM8996_MASTER_BIMC_SNOC_1,
	.buswidth = 16,
	.mas_rpm_id = 109,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_bimc_snoc_1_links),
	.links = mas_bimc_snoc_1_links
};

static const u16 mas_a0noc_snoc_links[] = {
	MSM8996_SLAVE_SNOC_PNOC,
	MSM8996_SLAVE_OCIMEM,
	MSM8996_SLAVE_APPSS,
	MSM8996_SLAVE_SNOC_BIMC,
	MSM8996_SLAVE_PIMEM
};

static struct qcom_icc_node mas_a0noc_snoc = {
	.name = "mas_a0noc_snoc",
	.id = MSM8996_MASTER_A0NOC_SNOC,
	.buswidth = 16,
	.mas_rpm_id = 110,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(mas_a0noc_snoc_links),
	.links = mas_a0noc_snoc_links
};

static const u16 mas_a1noc_snoc_links[] = {
	MSM8996_SLAVE_SNOC_VMEM,
	MSM8996_SLAVE_USB3,
	MSM8996_SLAVE_PCIE_0,
	MSM8996_SLAVE_PIMEM,
	MSM8996_SLAVE_PCIE_2,
	MSM8996_SLAVE_LPASS,
	MSM8996_SLAVE_PCIE_1,
	MSM8996_SLAVE_APPSS,
	MSM8996_SLAVE_SNOC_BIMC,
	MSM8996_SLAVE_SNOC_CNOC,
	MSM8996_SLAVE_SNOC_PNOC,
	MSM8996_SLAVE_OCIMEM,
	MSM8996_SLAVE_QDSS_STM
};

static struct qcom_icc_node mas_a1noc_snoc = {
	.name = "mas_a1noc_snoc",
	.id = MSM8996_MASTER_A1NOC_SNOC,
	.buswidth = 16,
	.mas_rpm_id = 111,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_a1noc_snoc_links),
	.links = mas_a1noc_snoc_links
};

static const u16 mas_a2noc_snoc_links[] = {
	MSM8996_SLAVE_SNOC_VMEM,
	MSM8996_SLAVE_USB3,
	MSM8996_SLAVE_PCIE_1,
	MSM8996_SLAVE_PIMEM,
	MSM8996_SLAVE_PCIE_2,
	MSM8996_SLAVE_QDSS_STM,
	MSM8996_SLAVE_LPASS,
	MSM8996_SLAVE_SNOC_BIMC,
	MSM8996_SLAVE_SNOC_CNOC,
	MSM8996_SLAVE_SNOC_PNOC,
	MSM8996_SLAVE_OCIMEM,
	MSM8996_SLAVE_PCIE_0
};

static struct qcom_icc_node mas_a2noc_snoc = {
	.name = "mas_a2noc_snoc",
	.id = MSM8996_MASTER_A2NOC_SNOC,
	.buswidth = 16,
	.mas_rpm_id = 112,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_a2noc_snoc_links),
	.links = mas_a2noc_snoc_links
};

static struct qcom_icc_node mas_qdss_etr = {
	.name = "mas_qdss_etr",
	.id = MSM8996_MASTER_QDSS_ETR,
	.buswidth = 16,
	.mas_rpm_id = 31,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 1,
	.qos.prio_level = 1,
	.qos.qos_port = 3,
	.num_links = ARRAY_SIZE(mas_qdss_common_links),
	.links = mas_qdss_common_links
};

static const u16 slv_a0noc_snoc_links[] = {
	MSM8996_MASTER_A0NOC_SNOC
};

static struct qcom_icc_node slv_a0noc_snoc = {
	.name = "slv_a0noc_snoc",
	.id = MSM8996_SLAVE_A0NOC_SNOC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 141,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_a0noc_snoc_links),
	.links = slv_a0noc_snoc_links
};

static const u16 slv_a1noc_snoc_links[] = {
	MSM8996_MASTER_A1NOC_SNOC
};

static struct qcom_icc_node slv_a1noc_snoc = {
	.name = "slv_a1noc_snoc",
	.id = MSM8996_SLAVE_A1NOC_SNOC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 142,
	.num_links = ARRAY_SIZE(slv_a1noc_snoc_links),
	.links = slv_a1noc_snoc_links
};

static const u16 slv_a2noc_snoc_links[] = {
	MSM8996_MASTER_A2NOC_SNOC
};

static struct qcom_icc_node slv_a2noc_snoc = {
	.name = "slv_a2noc_snoc",
	.id = MSM8996_SLAVE_A2NOC_SNOC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 143,
	.num_links = ARRAY_SIZE(slv_a2noc_snoc_links),
	.links = slv_a2noc_snoc_links
};

static struct qcom_icc_node slv_ebi = {
	.name = "slv_ebi",
	.id = MSM8996_SLAVE_EBI_CH0,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 0
};

static struct qcom_icc_node slv_hmss_l3 = {
	.name = "slv_hmss_l3",
	.id = MSM8996_SLAVE_HMSS_L3,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 160
};

static const u16 slv_bimc_snoc_0_links[] = {
	MSM8996_MASTER_BIMC_SNOC_0
};

static struct qcom_icc_node slv_bimc_snoc_0 = {
	.name = "slv_bimc_snoc_0",
	.id = MSM8996_SLAVE_BIMC_SNOC_0,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 2,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_bimc_snoc_0_links),
	.links = slv_bimc_snoc_0_links
};

static const u16 slv_bimc_snoc_1_links[] = {
	MSM8996_MASTER_BIMC_SNOC_1
};

static struct qcom_icc_node slv_bimc_snoc_1 = {
	.name = "slv_bimc_snoc_1",
	.id = MSM8996_SLAVE_BIMC_SNOC_1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 138,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_bimc_snoc_1_links),
	.links = slv_bimc_snoc_1_links
};

static const u16 slv_cnoc_a1noc_links[] = {
	MSM8996_MASTER_CNOC_A1NOC
};

static struct qcom_icc_node slv_cnoc_a1noc = {
	.name = "slv_cnoc_a1noc",
	.id = MSM8996_SLAVE_CNOC_A1NOC,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 75,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_cnoc_a1noc_links),
	.links = slv_cnoc_a1noc_links
};

static struct qcom_icc_node slv_clk_ctl = {
	.name = "slv_clk_ctl",
	.id = MSM8996_SLAVE_CLK_CTL,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 47
};

static struct qcom_icc_node slv_tcsr = {
	.name = "slv_tcsr",
	.id = MSM8996_SLAVE_TCSR,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 50
};

static struct qcom_icc_node slv_tlmm = {
	.name = "slv_tlmm",
	.id = MSM8996_SLAVE_TLMM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 51
};

static struct qcom_icc_node slv_crypto0_cfg = {
	.name = "slv_crypto0_cfg",
	.id = MSM8996_SLAVE_CRYPTO_0_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 52,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_mpm = {
	.name = "slv_mpm",
	.id = MSM8996_SLAVE_MPM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 62,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_pimem_cfg = {
	.name = "slv_pimem_cfg",
	.id = MSM8996_SLAVE_PIMEM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 167,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_imem_cfg = {
	.name = "slv_imem_cfg",
	.id = MSM8996_SLAVE_IMEM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 54,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_message_ram = {
	.name = "slv_message_ram",
	.id = MSM8996_SLAVE_MESSAGE_RAM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 55
};

static struct qcom_icc_node slv_bimc_cfg = {
	.name = "slv_bimc_cfg",
	.id = MSM8996_SLAVE_BIMC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 56,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_pmic_arb = {
	.name = "slv_pmic_arb",
	.id = MSM8996_SLAVE_PMIC_ARB,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 59
};

static struct qcom_icc_node slv_prng = {
	.name = "slv_prng",
	.id = MSM8996_SLAVE_PRNG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 127,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_dcc_cfg = {
	.name = "slv_dcc_cfg",
	.id = MSM8996_SLAVE_DCC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 155,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_rbcpr_mx = {
	.name = "slv_rbcpr_mx",
	.id = MSM8996_SLAVE_RBCPR_MX,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 170,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_qdss_cfg = {
	.name = "slv_qdss_cfg",
	.id = MSM8996_SLAVE_QDSS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 63,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_rbcpr_cx = {
	.name = "slv_rbcpr_cx",
	.id = MSM8996_SLAVE_RBCPR_CX,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 169,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_cpu_apu_cfg = {
	.name = "slv_cpu_apu_cfg",
	.id = MSM8996_SLAVE_QDSS_RBCPR_APU_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 168,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static const u16 slv_cnoc_mnoc_cfg_links[] = {
	MSM8996_MASTER_CNOC_MNOC_CFG
};

static struct qcom_icc_node slv_cnoc_mnoc_cfg = {
	.name = "slv_cnoc_mnoc_cfg",
	.id = MSM8996_SLAVE_CNOC_MNOC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 66,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_cnoc_mnoc_cfg_links),
	.links = slv_cnoc_mnoc_cfg_links
};

static struct qcom_icc_node slv_snoc_cfg = {
	.name = "slv_snoc_cfg",
	.id = MSM8996_SLAVE_SNOC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 70,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_snoc_mpu_cfg = {
	.name = "slv_snoc_mpu_cfg",
	.id = MSM8996_SLAVE_SNOC_MPU_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 67,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_ebi1_phy_cfg = {
	.name = "slv_ebi1_phy_cfg",
	.id = MSM8996_SLAVE_EBI1_PHY_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 73,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_a0noc_cfg = {
	.name = "slv_a0noc_cfg",
	.id = MSM8996_SLAVE_A0NOC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 144,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_pcie_1_cfg = {
	.name = "slv_pcie_1_cfg",
	.id = MSM8996_SLAVE_PCIE_1_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 89,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_pcie_2_cfg = {
	.name = "slv_pcie_2_cfg",
	.id = MSM8996_SLAVE_PCIE_2_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 165,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_pcie_0_cfg = {
	.name = "slv_pcie_0_cfg",
	.id = MSM8996_SLAVE_PCIE_0_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 88,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_pcie20_ahb2phy = {
	.name = "slv_pcie20_ahb2phy",
	.id = MSM8996_SLAVE_PCIE20_AHB2PHY,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 163,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_a0noc_mpu_cfg = {
	.name = "slv_a0noc_mpu_cfg",
	.id = MSM8996_SLAVE_A0NOC_MPU_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 145,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_ufs_cfg = {
	.name = "slv_ufs_cfg",
	.id = MSM8996_SLAVE_UFS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 92,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_a1noc_cfg = {
	.name = "slv_a1noc_cfg",
	.id = MSM8996_SLAVE_A1NOC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 147,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_a1noc_mpu_cfg = {
	.name = "slv_a1noc_mpu_cfg",
	.id = MSM8996_SLAVE_A1NOC_MPU_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 148,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_a2noc_cfg = {
	.name = "slv_a2noc_cfg",
	.id = MSM8996_SLAVE_A2NOC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 150,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_a2noc_mpu_cfg = {
	.name = "slv_a2noc_mpu_cfg",
	.id = MSM8996_SLAVE_A2NOC_MPU_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 151,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_ssc_cfg = {
	.name = "slv_ssc_cfg",
	.id = MSM8996_SLAVE_SSC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 177,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_a0noc_smmu_cfg = {
	.name = "slv_a0noc_smmu_cfg",
	.id = MSM8996_SLAVE_A0NOC_SMMU_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 146,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_a1noc_smmu_cfg = {
	.name = "slv_a1noc_smmu_cfg",
	.id = MSM8996_SLAVE_A1NOC_SMMU_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 149,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_a2noc_smmu_cfg = {
	.name = "slv_a2noc_smmu_cfg",
	.id = MSM8996_SLAVE_A2NOC_SMMU_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 152,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_lpass_smmu_cfg = {
	.name = "slv_lpass_smmu_cfg",
	.id = MSM8996_SLAVE_LPASS_SMMU_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 161,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static const u16 slv_cnoc_mnoc_mmss_cfg_links[] = {
	MSM8996_MASTER_CNOC_MNOC_MMSS_CFG
};

static struct qcom_icc_node slv_cnoc_mnoc_mmss_cfg = {
	.name = "slv_cnoc_mnoc_mmss_cfg",
	.id = MSM8996_SLAVE_CNOC_MNOC_MMSS_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 58,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_cnoc_mnoc_mmss_cfg_links),
	.links = slv_cnoc_mnoc_mmss_cfg_links
};

static struct qcom_icc_node slv_mmagic_cfg = {
	.name = "slv_mmagic_cfg",
	.id = MSM8996_SLAVE_MMAGIC_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 162,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_cpr_cfg = {
	.name = "slv_cpr_cfg",
	.id = MSM8996_SLAVE_CPR_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 6,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_misc_cfg = {
	.name = "slv_misc_cfg",
	.id = MSM8996_SLAVE_MISC_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 8,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_venus_throttle_cfg = {
	.name = "slv_venus_throttle_cfg",
	.id = MSM8996_SLAVE_VENUS_THROTTLE_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 178,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_venus_cfg = {
	.name = "slv_venus_cfg",
	.id = MSM8996_SLAVE_VENUS_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 10,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_vmem_cfg = {
	.name = "slv_vmem_cfg",
	.id = MSM8996_SLAVE_VMEM_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 180,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_dsa_cfg = {
	.name = "slv_dsa_cfg",
	.id = MSM8996_SLAVE_DSA_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 157,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_mnoc_clocks_cfg = {
	.name = "slv_mnoc_clocks_cfg",
	.id = MSM8996_SLAVE_MMSS_CLK_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 12,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_dsa_mpu_cfg = {
	.name = "slv_dsa_mpu_cfg",
	.id = MSM8996_SLAVE_DSA_MPU_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 158,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_mnoc_mpu_cfg = {
	.name = "slv_mnoc_mpu_cfg",
	.id = MSM8996_SLAVE_MNOC_MPU_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 14,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_display_cfg = {
	.name = "slv_display_cfg",
	.id = MSM8996_SLAVE_DISPLAY_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 4,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_display_throttle_cfg = {
	.name = "slv_display_throttle_cfg",
	.id = MSM8996_SLAVE_DISPLAY_THROTTLE_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 156,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_camera_cfg = {
	.name = "slv_camera_cfg",
	.id = MSM8996_SLAVE_CAMERA_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 3,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_camera_throttle_cfg = {
	.name = "slv_camera_throttle_cfg",
	.id = MSM8996_SLAVE_CAMERA_THROTTLE_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 154,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_oxili_cfg = {
	.name = "slv_oxili_cfg",
	.id = MSM8996_SLAVE_GRAPHICS_3D_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 11,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_smmu_mdp_cfg = {
	.name = "slv_smmu_mdp_cfg",
	.id = MSM8996_SLAVE_SMMU_MDP_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 173,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_smmu_rot_cfg = {
	.name = "slv_smmu_rot_cfg",
	.id = MSM8996_SLAVE_SMMU_ROTATOR_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 174,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_smmu_venus_cfg = {
	.name = "slv_smmu_venus_cfg",
	.id = MSM8996_SLAVE_SMMU_VENUS_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 175,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_smmu_cpp_cfg = {
	.name = "slv_smmu_cpp_cfg",
	.id = MSM8996_SLAVE_SMMU_CPP_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 171,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_smmu_jpeg_cfg = {
	.name = "slv_smmu_jpeg_cfg",
	.id = MSM8996_SLAVE_SMMU_JPEG_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 172,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_smmu_vfe_cfg = {
	.name = "slv_smmu_vfe_cfg",
	.id = MSM8996_SLAVE_SMMU_VFE_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 176,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static const u16 slv_mnoc_bimc_links[] = {
	MSM8996_MASTER_MNOC_BIMC
};

static struct qcom_icc_node slv_mnoc_bimc = {
	.name = "slv_mnoc_bimc",
	.id = MSM8996_SLAVE_MNOC_BIMC,
	.buswidth = 32,
	.mas_rpm_id = -1,
	.slv_rpm_id = 16,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_mnoc_bimc_links),
	.links = slv_mnoc_bimc_links
};

static struct qcom_icc_node slv_vmem = {
	.name = "slv_vmem",
	.id = MSM8996_SLAVE_VMEM,
	.buswidth = 32,
	.mas_rpm_id = -1,
	.slv_rpm_id = 179,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_srvc_mnoc = {
	.name = "slv_srvc_mnoc",
	.id = MSM8996_SLAVE_SERVICE_MNOC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 17,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static const u16 slv_pnoc_a1noc_links[] = {
	MSM8996_MASTER_PNOC_A1NOC
};

static struct qcom_icc_node slv_pnoc_a1noc = {
	.name = "slv_pnoc_a1noc",
	.id = MSM8996_SLAVE_PNOC_A1NOC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 139,
	.num_links = ARRAY_SIZE(slv_pnoc_a1noc_links),
	.links = slv_pnoc_a1noc_links
};

static struct qcom_icc_node slv_usb_hs = {
	.name = "slv_usb_hs",
	.id = MSM8996_SLAVE_USB_HS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 40
};

static struct qcom_icc_node slv_sdcc_2 = {
	.name = "slv_sdcc_2",
	.id = MSM8996_SLAVE_SDCC_2,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 33
};

static struct qcom_icc_node slv_sdcc_4 = {
	.name = "slv_sdcc_4",
	.id = MSM8996_SLAVE_SDCC_4,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 34
};

static struct qcom_icc_node slv_tsif = {
	.name = "slv_tsif",
	.id = MSM8996_SLAVE_TSIF,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 35
};

static struct qcom_icc_node slv_blsp_2 = {
	.name = "slv_blsp_2",
	.id = MSM8996_SLAVE_BLSP_2,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 37
};

static struct qcom_icc_node slv_sdcc_1 = {
	.name = "slv_sdcc_1",
	.id = MSM8996_SLAVE_SDCC_1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 31
};

static struct qcom_icc_node slv_blsp_1 = {
	.name = "slv_blsp_1",
	.id = MSM8996_SLAVE_BLSP_1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 39
};

static struct qcom_icc_node slv_pdm = {
	.name = "slv_pdm",
	.id = MSM8996_SLAVE_PDM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 41
};

static struct qcom_icc_node slv_ahb2phy = {
	.name = "slv_ahb2phy",
	.id = MSM8996_SLAVE_AHB2PHY,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 153,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_hmss = {
	.name = "slv_hmss",
	.id = MSM8996_SLAVE_APPSS,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 20,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_lpass = {
	.name = "slv_lpass",
	.id = MSM8996_SLAVE_LPASS,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 21,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_usb3 = {
	.name = "slv_usb3",
	.id = MSM8996_SLAVE_USB3,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 22,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static const u16 slv_snoc_bimc_links[] = {
	MSM8996_MASTER_SNOC_BIMC
};

static struct qcom_icc_node slv_snoc_bimc = {
	.name = "slv_snoc_bimc",
	.id = MSM8996_SLAVE_SNOC_BIMC,
	.buswidth = 32,
	.mas_rpm_id = -1,
	.slv_rpm_id = 24,
	.num_links = ARRAY_SIZE(slv_snoc_bimc_links),
	.links = slv_snoc_bimc_links
};

static const u16 slv_snoc_cnoc_links[] = {
	MSM8996_MASTER_SNOC_CNOC
};

static struct qcom_icc_node slv_snoc_cnoc = {
	.name = "slv_snoc_cnoc",
	.id = MSM8996_SLAVE_SNOC_CNOC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 25,
	.num_links = ARRAY_SIZE(slv_snoc_cnoc_links),
	.links = slv_snoc_cnoc_links
};

static struct qcom_icc_node slv_imem = {
	.name = "slv_imem",
	.id = MSM8996_SLAVE_OCIMEM,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 26
};

static struct qcom_icc_node slv_pimem = {
	.name = "slv_pimem",
	.id = MSM8996_SLAVE_PIMEM,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 166
};

static const u16 slv_snoc_vmem_links[] = {
	MSM8996_MASTER_SNOC_VMEM
};

static struct qcom_icc_node slv_snoc_vmem = {
	.name = "slv_snoc_vmem",
	.id = MSM8996_SLAVE_SNOC_VMEM,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 140,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.num_links = ARRAY_SIZE(slv_snoc_vmem_links),
	.links = slv_snoc_vmem_links
};

static const u16 slv_snoc_pnoc_links[] = {
	MSM8996_MASTER_SNOC_PNOC
};

static struct qcom_icc_node slv_snoc_pnoc = {
	.name = "slv_snoc_pnoc",
	.id = MSM8996_SLAVE_SNOC_PNOC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 28,
	.num_links = ARRAY_SIZE(slv_snoc_pnoc_links),
	.links = slv_snoc_pnoc_links
};

static struct qcom_icc_node slv_qdss_stm = {
	.name = "slv_qdss_stm",
	.id = MSM8996_SLAVE_QDSS_STM,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 30
};

static struct qcom_icc_node slv_pcie_0 = {
	.name = "slv_pcie_0",
	.id = MSM8996_SLAVE_PCIE_0,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 84,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_pcie_1 = {
	.name = "slv_pcie_1",
	.id = MSM8996_SLAVE_PCIE_1,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 85,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_pcie_2 = {
	.name = "slv_pcie_2",
	.id = MSM8996_SLAVE_PCIE_2,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 164,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node slv_srvc_snoc = {
	.name = "slv_srvc_snoc",
	.id = MSM8996_SLAVE_SERVICE_SNOC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 29,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID
};

static struct qcom_icc_node * const a0noc_nodes[] = {
	[MASTER_PCIE_0] = &mas_pcie_0,
	[MASTER_PCIE_1] = &mas_pcie_1,
	[MASTER_PCIE_2] = &mas_pcie_2
};

static const struct regmap_config msm8996_a0noc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x9000,
	.fast_io	= true
};

static const struct qcom_icc_desc msm8996_a0noc = {
	.type = QCOM_ICC_NOC,
	.nodes = a0noc_nodes,
	.num_nodes = ARRAY_SIZE(a0noc_nodes),
	.clocks = bus_a0noc_clocks,
	.num_clocks = ARRAY_SIZE(bus_a0noc_clocks),
	.has_bus_pd = true,
	.regmap_cfg = &msm8996_a0noc_regmap_config
};

static struct qcom_icc_node * const a1noc_nodes[] = {
	[MASTER_CNOC_A1NOC] = &mas_cnoc_a1noc,
	[MASTER_CRYPTO_CORE0] = &mas_crypto_c0,
	[MASTER_PNOC_A1NOC] = &mas_pnoc_a1noc
};

static const struct regmap_config msm8996_a1noc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x7000,
	.fast_io	= true
};

static const struct qcom_icc_desc msm8996_a1noc = {
	.type = QCOM_ICC_NOC,
	.nodes = a1noc_nodes,
	.num_nodes = ARRAY_SIZE(a1noc_nodes),
	.regmap_cfg = &msm8996_a1noc_regmap_config
};

static struct qcom_icc_node * const a2noc_nodes[] = {
	[MASTER_USB3] = &mas_usb3,
	[MASTER_IPA] = &mas_ipa,
	[MASTER_UFS] = &mas_ufs
};

static const struct regmap_config msm8996_a2noc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0xa000,
	.fast_io	= true
};

static const struct qcom_icc_desc msm8996_a2noc = {
	.type = QCOM_ICC_NOC,
	.nodes = a2noc_nodes,
	.num_nodes = ARRAY_SIZE(a2noc_nodes),
	.regmap_cfg = &msm8996_a2noc_regmap_config
};

static struct qcom_icc_node * const bimc_nodes[] = {
	[MASTER_AMPSS_M0] = &mas_apps_proc,
	[MASTER_GRAPHICS_3D] = &mas_oxili,
	[MASTER_MNOC_BIMC] = &mas_mnoc_bimc,
	[MASTER_SNOC_BIMC] = &mas_snoc_bimc,
	[SLAVE_EBI_CH0] = &slv_ebi,
	[SLAVE_HMSS_L3] = &slv_hmss_l3,
	[SLAVE_BIMC_SNOC_0] = &slv_bimc_snoc_0,
	[SLAVE_BIMC_SNOC_1] = &slv_bimc_snoc_1
};

static const struct regmap_config msm8996_bimc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x62000,
	.fast_io	= true
};

static const struct qcom_icc_desc msm8996_bimc = {
	.type = QCOM_ICC_BIMC,
	.nodes = bimc_nodes,
	.num_nodes = ARRAY_SIZE(bimc_nodes),
	.regmap_cfg = &msm8996_bimc_regmap_config
};

static struct qcom_icc_node * const cnoc_nodes[] = {
	[MASTER_SNOC_CNOC] = &mas_snoc_cnoc,
	[MASTER_QDSS_DAP] = &mas_qdss_dap,
	[SLAVE_CNOC_A1NOC] = &slv_cnoc_a1noc,
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
	[SLAVE_CNOC_MNOC_CFG] = &slv_cnoc_mnoc_cfg,
	[SLAVE_SNOC_CFG] = &slv_snoc_cfg,
	[SLAVE_SNOC_MPU_CFG] = &slv_snoc_mpu_cfg,
	[SLAVE_EBI1_PHY_CFG] = &slv_ebi1_phy_cfg,
	[SLAVE_A0NOC_CFG] = &slv_a0noc_cfg,
	[SLAVE_PCIE_1_CFG] = &slv_pcie_1_cfg,
	[SLAVE_PCIE_2_CFG] = &slv_pcie_2_cfg,
	[SLAVE_PCIE_0_CFG] = &slv_pcie_0_cfg,
	[SLAVE_PCIE20_AHB2PHY] = &slv_pcie20_ahb2phy,
	[SLAVE_A0NOC_MPU_CFG] = &slv_a0noc_mpu_cfg,
	[SLAVE_UFS_CFG] = &slv_ufs_cfg,
	[SLAVE_A1NOC_CFG] = &slv_a1noc_cfg,
	[SLAVE_A1NOC_MPU_CFG] = &slv_a1noc_mpu_cfg,
	[SLAVE_A2NOC_CFG] = &slv_a2noc_cfg,
	[SLAVE_A2NOC_MPU_CFG] = &slv_a2noc_mpu_cfg,
	[SLAVE_SSC_CFG] = &slv_ssc_cfg,
	[SLAVE_A0NOC_SMMU_CFG] = &slv_a0noc_smmu_cfg,
	[SLAVE_A1NOC_SMMU_CFG] = &slv_a1noc_smmu_cfg,
	[SLAVE_A2NOC_SMMU_CFG] = &slv_a2noc_smmu_cfg,
	[SLAVE_LPASS_SMMU_CFG] = &slv_lpass_smmu_cfg,
	[SLAVE_CNOC_MNOC_MMSS_CFG] = &slv_cnoc_mnoc_mmss_cfg
};

static const struct regmap_config msm8996_cnoc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x1000,
	.fast_io	= true
};

static const struct qcom_icc_desc msm8996_cnoc = {
	.type = QCOM_ICC_NOC,
	.nodes = cnoc_nodes,
	.num_nodes = ARRAY_SIZE(cnoc_nodes),
	.regmap_cfg = &msm8996_cnoc_regmap_config
};

static struct qcom_icc_node * const mnoc_nodes[] = {
	[MASTER_CNOC_MNOC_CFG] = &mas_cnoc_mnoc_cfg,
	[MASTER_CPP] = &mas_cpp,
	[MASTER_JPEG] = &mas_jpeg,
	[MASTER_MDP_PORT0] = &mas_mdp_p0,
	[MASTER_MDP_PORT1] = &mas_mdp_p1,
	[MASTER_ROTATOR] = &mas_rotator,
	[MASTER_VIDEO_P0] = &mas_venus,
	[MASTER_VFE] = &mas_vfe,
	[MASTER_SNOC_VMEM] = &mas_snoc_vmem,
	[MASTER_VIDEO_P0_OCMEM] = &mas_venus_vmem,
	[MASTER_CNOC_MNOC_MMSS_CFG] = &mas_cnoc_mnoc_mmss_cfg,
	[SLAVE_MNOC_BIMC] = &slv_mnoc_bimc,
	[SLAVE_VMEM] = &slv_vmem,
	[SLAVE_SERVICE_MNOC] = &slv_srvc_mnoc,
	[SLAVE_MMAGIC_CFG] = &slv_mmagic_cfg,
	[SLAVE_CPR_CFG] = &slv_cpr_cfg,
	[SLAVE_MISC_CFG] = &slv_misc_cfg,
	[SLAVE_VENUS_THROTTLE_CFG] = &slv_venus_throttle_cfg,
	[SLAVE_VENUS_CFG] = &slv_venus_cfg,
	[SLAVE_VMEM_CFG] = &slv_vmem_cfg,
	[SLAVE_DSA_CFG] = &slv_dsa_cfg,
	[SLAVE_MMSS_CLK_CFG] = &slv_mnoc_clocks_cfg,
	[SLAVE_DSA_MPU_CFG] = &slv_dsa_mpu_cfg,
	[SLAVE_MNOC_MPU_CFG] = &slv_mnoc_mpu_cfg,
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

static const struct regmap_config msm8996_mnoc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x20000,
	.fast_io	= true
};

static const struct qcom_icc_desc msm8996_mnoc = {
	.type = QCOM_ICC_NOC,
	.nodes = mnoc_nodes,
	.num_nodes = ARRAY_SIZE(mnoc_nodes),
	.clocks = bus_mm_clocks,
	.num_clocks = ARRAY_SIZE(bus_mm_clocks),
	.regmap_cfg = &msm8996_mnoc_regmap_config
};

static struct qcom_icc_node * const pnoc_nodes[] = {
	[MASTER_SNOC_PNOC] = &mas_snoc_pnoc,
	[MASTER_SDCC_1] = &mas_sdcc_1,
	[MASTER_SDCC_2] = &mas_sdcc_2,
	[MASTER_SDCC_4] = &mas_sdcc_4,
	[MASTER_USB_HS] = &mas_usb_hs,
	[MASTER_BLSP_1] = &mas_blsp_1,
	[MASTER_BLSP_2] = &mas_blsp_2,
	[MASTER_TSIF] = &mas_tsif,
	[SLAVE_PNOC_A1NOC] = &slv_pnoc_a1noc,
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

static const struct regmap_config msm8996_pnoc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x3000,
	.fast_io	= true
};

static const struct qcom_icc_desc msm8996_pnoc = {
	.type = QCOM_ICC_NOC,
	.nodes = pnoc_nodes,
	.num_nodes = ARRAY_SIZE(pnoc_nodes),
	.regmap_cfg = &msm8996_pnoc_regmap_config
};

static struct qcom_icc_node * const snoc_nodes[] = {
	[MASTER_HMSS] = &mas_hmss,
	[MASTER_QDSS_BAM] = &mas_qdss_bam,
	[MASTER_SNOC_CFG] = &mas_snoc_cfg,
	[MASTER_BIMC_SNOC_0] = &mas_bimc_snoc_0,
	[MASTER_BIMC_SNOC_1] = &mas_bimc_snoc_1,
	[MASTER_A0NOC_SNOC] = &mas_a0noc_snoc,
	[MASTER_A1NOC_SNOC] = &mas_a1noc_snoc,
	[MASTER_A2NOC_SNOC] = &mas_a2noc_snoc,
	[MASTER_QDSS_ETR] = &mas_qdss_etr,
	[SLAVE_A0NOC_SNOC] = &slv_a0noc_snoc,
	[SLAVE_A1NOC_SNOC] = &slv_a1noc_snoc,
	[SLAVE_A2NOC_SNOC] = &slv_a2noc_snoc,
	[SLAVE_HMSS] = &slv_hmss,
	[SLAVE_LPASS] = &slv_lpass,
	[SLAVE_USB3] = &slv_usb3,
	[SLAVE_SNOC_BIMC] = &slv_snoc_bimc,
	[SLAVE_SNOC_CNOC] = &slv_snoc_cnoc,
	[SLAVE_IMEM] = &slv_imem,
	[SLAVE_PIMEM] = &slv_pimem,
	[SLAVE_SNOC_VMEM] = &slv_snoc_vmem,
	[SLAVE_SNOC_PNOC] = &slv_snoc_pnoc,
	[SLAVE_QDSS_STM] = &slv_qdss_stm,
	[SLAVE_PCIE_0] = &slv_pcie_0,
	[SLAVE_PCIE_1] = &slv_pcie_1,
	[SLAVE_PCIE_2] = &slv_pcie_2,
	[SLAVE_SERVICE_SNOC] = &slv_srvc_snoc
};

static const struct regmap_config msm8996_snoc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x20000,
	.fast_io	= true
};

static const struct qcom_icc_desc msm8996_snoc = {
	.type = QCOM_ICC_NOC,
	.nodes = snoc_nodes,
	.num_nodes = ARRAY_SIZE(snoc_nodes),
	.regmap_cfg = &msm8996_snoc_regmap_config
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,msm8996-a0noc", .data = &msm8996_a0noc},
	{ .compatible = "qcom,msm8996-a1noc", .data = &msm8996_a1noc},
	{ .compatible = "qcom,msm8996-a2noc", .data = &msm8996_a2noc},
	{ .compatible = "qcom,msm8996-bimc", .data = &msm8996_bimc},
	{ .compatible = "qcom,msm8996-cnoc", .data = &msm8996_cnoc},
	{ .compatible = "qcom,msm8996-mnoc", .data = &msm8996_mnoc},
	{ .compatible = "qcom,msm8996-pnoc", .data = &msm8996_pnoc},
	{ .compatible = "qcom,msm8996-snoc", .data = &msm8996_snoc},
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qnoc_probe,
	.remove = qnoc_remove,
	.driver = {
		.name = "qnoc-msm8996",
		.of_match_table = qnoc_of_match,
		.sync_state = icc_sync_state,
	}
};
module_platform_driver(qnoc_driver);

MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_DESCRIPTION("Qualcomm MSM8996 NoC driver");
MODULE_LICENSE("GPL v2");
