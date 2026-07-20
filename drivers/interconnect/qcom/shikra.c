// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 */

#include <dt-bindings/interconnect/qcom,shikra.h>
#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "icc-rpm.h"

static const char * const sys_noc_intf_clocks[] = {
	"emac0_axi",
	"emac1_axi",
	"usb2_axi",
	"usb3_axi",
};

static const char * const memnoc_intf_clocks[] = {
	"gpu_axi",
};

enum {
	SHIKRA_MASTER_QUP_CORE_0 = 1,
	SHIKRA_SNOC_CNOC_MAS,
	SHIKRA_MASTER_QDSS_DAP,
	SHIKRA_MASTER_LLCC,
	SHIKRA_MASTER_GRAPHICS_3D,
	SHIKRA_MASTER_MNOC_HF_MEM_NOC,
	SHIKRA_MASTER_ANOC_PCIE_MEM_NOC,
	SHIKRA_MASTER_SNOC_SF_MEM_NOC,
	SHIKRA_MASTER_AMPSS_M0,
	SHIKRA_MASTER_SYS_TCU,
	SHIKRA_MASTER_CAMNOC_SF,
	SHIKRA_MASTER_VIDEO_P0,
	SHIKRA_MASTER_VIDEO_PROC,
	SHIKRA_MASTER_CAMNOC_HF,
	SHIKRA_MASTER_MDP_PORT0,
	SHIKRA_MASTER_MMRT_VIRT,
	SHIKRA_MASTER_SNOC_CFG,
	SHIKRA_MASTER_TIC,
	SHIKRA_MASTER_ANOC_SNOC,
	SHIKRA_MASTER_MEMNOC_PCIE,
	SHIKRA_MASTER_MEMNOC_SNOC,
	SHIKRA_MASTER_PIMEM,
	SHIKRA_MASTER_PCIE2_0,
	SHIKRA_MASTER_QDSS_BAM,
	SHIKRA_MASTER_QPIC,
	SHIKRA_MASTER_QUP_0,
	SHIKRA_CNOC_SNOC_MAS,
	SHIKRA_MASTER_AUDIO,
	SHIKRA_MASTER_EMAC_0,
	SHIKRA_MASTER_EMAC_1,
	SHIKRA_MASTER_QDSS_ETR,
	SHIKRA_MASTER_SDCC_1,
	SHIKRA_MASTER_SDCC_2,
	SHIKRA_MASTER_USB2_0,
	SHIKRA_MASTER_USB3,
	SHIKRA_MASTER_CRYPTO_CORE0,

	SHIKRA_SLAVE_QUP_CORE_0,
	SHIKRA_SLAVE_AHB2PHY_USB,
	SHIKRA_SLAVE_APSS_THROTTLE_CFG,
	SHIKRA_SLAVE_AUDIO,
	SHIKRA_SLAVE_BOOT_ROM,
	SHIKRA_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	SHIKRA_SLAVE_CAMERA_CFG,
	SHIKRA_SLAVE_CDSP_THROTTLE_CFG,
	SHIKRA_SLAVE_CLK_CTL,
	SHIKRA_SLAVE_DSP_CFG,
	SHIKRA_SLAVE_RBCPR_CX_CFG,
	SHIKRA_SLAVE_RBCPR_MX_CFG,
	SHIKRA_SLAVE_CRYPTO_0_CFG,
	SHIKRA_SLAVE_DDR_SS_CFG,
	SHIKRA_SLAVE_DISPLAY_CFG,
	SHIKRA_SLAVE_EMAC0_CFG,
	SHIKRA_SLAVE_EMAC1_CFG,
	SHIKRA_SLAVE_GPU_CFG,
	SHIKRA_SLAVE_GPU_THROTTLE_CFG,
	SHIKRA_SLAVE_HWKM,
	SHIKRA_SLAVE_IMEM_CFG,
	SHIKRA_SLAVE_MAPSS,
	SHIKRA_SLAVE_MDSP_MPU_CFG,
	SHIKRA_SLAVE_MESSAGE_RAM,
	SHIKRA_SLAVE_MSS,
	SHIKRA_SLAVE_PCIE_CFG,
	SHIKRA_SLAVE_PDM,
	SHIKRA_SLAVE_PIMEM_CFG,
	SHIKRA_SLAVE_PKA_WRAPPER_CFG,
	SHIKRA_SLAVE_PMIC_ARB,
	SHIKRA_SLAVE_QDSS_CFG,
	SHIKRA_SLAVE_QM_CFG,
	SHIKRA_SLAVE_QM_MPU_CFG,
	SHIKRA_SLAVE_QPIC,
	SHIKRA_SLAVE_QUP_0,
	SHIKRA_SLAVE_RPM,
	SHIKRA_SLAVE_SDCC_1,
	SHIKRA_SLAVE_SDCC_2,
	SHIKRA_SLAVE_SECURITY,
	SHIKRA_SLAVE_SNOC_CFG,
	SHIKRA_SNOC_SF_THROTTLE_CFG,
	SHIKRA_SLAVE_TLMM,
	SHIKRA_SLAVE_TSCSS,
	SHIKRA_SLAVE_USB2,
	SHIKRA_SLAVE_USB3,
	SHIKRA_SLAVE_VENUS_CFG,
	SHIKRA_SLAVE_VENUS_THROTTLE_CFG,
	SHIKRA_SLAVE_VSENSE_CTRL_CFG,
	SHIKRA_SLAVE_SERVICE_CNOC,
	SHIKRA_SLAVE_EBI_CH0,
	SHIKRA_SLAVE_LLCC,
	SHIKRA_SLAVE_MEMNOC_SNOC,
	SHIKRA_SLAVE_MEM_NOC_PCIE_SNOC,
	SHIKRA_SLAVE_MMNRT_VIRT,
	SHIKRA_SLAVE_MM_MEMNOC,
	SHIKRA_SLAVE_APPSS,
	SHIKRA_SLAVE_MCUSS,
	SHIKRA_SLAVE_WCSS,
	SHIKRA_SLAVE_MEMNOC_SF,
	SHIKRA_SNOC_CNOC_SLV,
	SHIKRA_SLAVE_BOOTIMEM,
	SHIKRA_SLAVE_OCIMEM,
	SHIKRA_SLAVE_PIMEM,
	SHIKRA_SLAVE_SERVICE_SNOC,
	SHIKRA_SLAVE_PCIE2_0,
	SHIKRA_SLAVE_QDSS_STM,
	SHIKRA_SLAVE_TCU,
	SHIKRA_SLAVE_PCIE_MEMNOC,
	SHIKRA_SLAVE_ANOC_SNOC,
};

/* Master nodes */
static const u16 qup0_core_master_links[] = {
	SHIKRA_SLAVE_QUP_CORE_0,
};

static struct qcom_icc_node qup0_core_master = {
	.id = SHIKRA_MASTER_QUP_CORE_0,
	.name = "qup0_core_master",
	.buswidth = 4,
	.mas_rpm_id = 170,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qup0_core_master_links),
	.links = qup0_core_master_links,
};

static const u16 qnm_snoc_cnoc_links[] = {
	SHIKRA_SLAVE_AHB2PHY_USB,
	SHIKRA_SLAVE_APSS_THROTTLE_CFG,
	SHIKRA_SLAVE_AUDIO,
	SHIKRA_SLAVE_BOOT_ROM,
	SHIKRA_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	SHIKRA_SLAVE_CAMERA_CFG,
	SHIKRA_SLAVE_CDSP_THROTTLE_CFG,
	SHIKRA_SLAVE_CLK_CTL,
	SHIKRA_SLAVE_DSP_CFG,
	SHIKRA_SLAVE_RBCPR_CX_CFG,
	SHIKRA_SLAVE_RBCPR_MX_CFG,
	SHIKRA_SLAVE_CRYPTO_0_CFG,
	SHIKRA_SLAVE_DDR_SS_CFG,
	SHIKRA_SLAVE_DISPLAY_CFG,
	SHIKRA_SLAVE_EMAC0_CFG,
	SHIKRA_SLAVE_EMAC1_CFG,
	SHIKRA_SLAVE_GPU_CFG,
	SHIKRA_SLAVE_GPU_THROTTLE_CFG,
	SHIKRA_SLAVE_HWKM,
	SHIKRA_SLAVE_IMEM_CFG,
	SHIKRA_SLAVE_MAPSS,
	SHIKRA_SLAVE_MDSP_MPU_CFG,
	SHIKRA_SLAVE_MESSAGE_RAM,
	SHIKRA_SLAVE_MSS,
	SHIKRA_SLAVE_PCIE_CFG,
	SHIKRA_SLAVE_PDM,
	SHIKRA_SLAVE_PIMEM_CFG,
	SHIKRA_SLAVE_PKA_WRAPPER_CFG,
	SHIKRA_SLAVE_PMIC_ARB,
	SHIKRA_SLAVE_QDSS_CFG,
	SHIKRA_SLAVE_QM_CFG,
	SHIKRA_SLAVE_QM_MPU_CFG,
	SHIKRA_SLAVE_QPIC,
	SHIKRA_SLAVE_QUP_0,
	SHIKRA_SLAVE_RPM,
	SHIKRA_SLAVE_SDCC_1,
	SHIKRA_SLAVE_SDCC_2,
	SHIKRA_SLAVE_SECURITY,
	SHIKRA_SLAVE_SNOC_CFG,
	SHIKRA_SNOC_SF_THROTTLE_CFG,
	SHIKRA_SLAVE_TLMM,
	SHIKRA_SLAVE_TSCSS,
	SHIKRA_SLAVE_USB2,
	SHIKRA_SLAVE_USB3,
	SHIKRA_SLAVE_VENUS_CFG,
	SHIKRA_SLAVE_VENUS_THROTTLE_CFG,
	SHIKRA_SLAVE_VSENSE_CTRL_CFG,
	SHIKRA_SLAVE_SERVICE_CNOC,
};

static struct qcom_icc_node qnm_snoc_cnoc = {
	.id = SHIKRA_SNOC_CNOC_MAS,
	.name = "qnm_snoc_cnoc",
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qnm_snoc_cnoc_links),
	.links = qnm_snoc_cnoc_links,
};

static const u16 xm_dap_links[] = {
	SHIKRA_SLAVE_AHB2PHY_USB,
	SHIKRA_SLAVE_APSS_THROTTLE_CFG,
	SHIKRA_SLAVE_AUDIO,
	SHIKRA_SLAVE_BOOT_ROM,
	SHIKRA_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	SHIKRA_SLAVE_CAMERA_CFG,
	SHIKRA_SLAVE_CDSP_THROTTLE_CFG,
	SHIKRA_SLAVE_CLK_CTL,
	SHIKRA_SLAVE_DSP_CFG,
	SHIKRA_SLAVE_RBCPR_CX_CFG,
	SHIKRA_SLAVE_RBCPR_MX_CFG,
	SHIKRA_SLAVE_CRYPTO_0_CFG,
	SHIKRA_SLAVE_DDR_SS_CFG,
	SHIKRA_SLAVE_DISPLAY_CFG,
	SHIKRA_SLAVE_EMAC0_CFG,
	SHIKRA_SLAVE_EMAC1_CFG,
	SHIKRA_SLAVE_GPU_CFG,
	SHIKRA_SLAVE_GPU_THROTTLE_CFG,
	SHIKRA_SLAVE_HWKM,
	SHIKRA_SLAVE_IMEM_CFG,
	SHIKRA_SLAVE_MAPSS,
	SHIKRA_SLAVE_MDSP_MPU_CFG,
	SHIKRA_SLAVE_MESSAGE_RAM,
	SHIKRA_SLAVE_MSS,
	SHIKRA_SLAVE_PCIE_CFG,
	SHIKRA_SLAVE_PDM,
	SHIKRA_SLAVE_PIMEM_CFG,
	SHIKRA_SLAVE_PKA_WRAPPER_CFG,
	SHIKRA_SLAVE_PMIC_ARB,
	SHIKRA_SLAVE_QDSS_CFG,
	SHIKRA_SLAVE_QM_CFG,
	SHIKRA_SLAVE_QM_MPU_CFG,
	SHIKRA_SLAVE_QPIC,
	SHIKRA_SLAVE_QUP_0,
	SHIKRA_SLAVE_RPM,
	SHIKRA_SLAVE_SDCC_1,
	SHIKRA_SLAVE_SDCC_2,
	SHIKRA_SLAVE_SECURITY,
	SHIKRA_SLAVE_SNOC_CFG,
	SHIKRA_SNOC_SF_THROTTLE_CFG,
	SHIKRA_SLAVE_TLMM,
	SHIKRA_SLAVE_TSCSS,
	SHIKRA_SLAVE_USB2,
	SHIKRA_SLAVE_USB3,
	SHIKRA_SLAVE_VENUS_CFG,
	SHIKRA_SLAVE_VENUS_THROTTLE_CFG,
	SHIKRA_SLAVE_VSENSE_CTRL_CFG,
	SHIKRA_SLAVE_SERVICE_CNOC,
};

static struct qcom_icc_node xm_dap = {
	.id = SHIKRA_MASTER_QDSS_DAP,
	.name = "xm_dap",
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(xm_dap_links),
	.links = xm_dap_links,
};

static const u16 llcc_mc_links[] = {
	SHIKRA_SLAVE_EBI_CH0,
};

static struct qcom_icc_node llcc_mc = {
	.id = SHIKRA_MASTER_LLCC,
	.name = "llcc_mc",
	.buswidth = 4,
	.channels = 2,
	.mas_rpm_id = 190,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(llcc_mc_links),
	.links = llcc_mc_links,
};

static const u16 qnm_gpu_links[] = {
	SHIKRA_SLAVE_LLCC,
	SHIKRA_SLAVE_MEMNOC_SNOC,
	SHIKRA_SLAVE_MEM_NOC_PCIE_SNOC,
};

static struct qcom_icc_node qnm_gpu = {
	.id = SHIKRA_MASTER_GRAPHICS_3D,
	.name = "qnm_gpu",
	.buswidth = 16,
	.qos.ap_owned = true,
	.qos.qos_port = 6,
	.qos.urg_fwd_en = false,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qnm_gpu_links),
	.links = qnm_gpu_links,
};

static const u16 qnm_mnoc_hf_links[] = {
	SHIKRA_SLAVE_LLCC,
	SHIKRA_SLAVE_MEMNOC_SNOC,
	SHIKRA_SLAVE_MEM_NOC_PCIE_SNOC,
};

static struct qcom_icc_node qnm_mnoc_hf = {
	.id = SHIKRA_MASTER_MNOC_HF_MEM_NOC,
	.name = "qnm_mnoc_hf",
	.buswidth = 16,
	.qos.ap_owned = true,
	.qos.qos_port = 7,
	.qos.urg_fwd_en = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qnm_mnoc_hf_links),
	.links = qnm_mnoc_hf_links,
};

static const u16 qnm_pcie_links[] = {
	SHIKRA_SLAVE_LLCC,
	SHIKRA_SLAVE_MEMNOC_SNOC,
};

static struct qcom_icc_node qnm_pcie = {
	.id = SHIKRA_MASTER_ANOC_PCIE_MEM_NOC,
	.name = "qnm_pcie",
	.buswidth = 8,
	.qos.ap_owned = true,
	.qos.qos_port = 4,
	.qos.urg_fwd_en = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.mas_rpm_id = 185,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qnm_pcie_links),
	.links = qnm_pcie_links,
};

static const u16 qnm_snoc_sf_links[] = {
	SHIKRA_SLAVE_LLCC,
	SHIKRA_SLAVE_MEMNOC_SNOC,
	SHIKRA_SLAVE_MEM_NOC_PCIE_SNOC,
};

static struct qcom_icc_node qnm_snoc_sf = {
	.id = SHIKRA_MASTER_SNOC_SF_MEM_NOC,
	.name = "qnm_snoc_sf",
	.buswidth = 16,
	.qos.ap_owned = true,
	.qos.qos_port = 3,
	.qos.urg_fwd_en = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.mas_rpm_id = 76,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qnm_snoc_sf_links),
	.links = qnm_snoc_sf_links,
};

static const u16 xm_apps_links[] = {
	SHIKRA_SLAVE_LLCC,
	SHIKRA_SLAVE_MEMNOC_SNOC,
	SHIKRA_SLAVE_MEM_NOC_PCIE_SNOC,
};

static struct qcom_icc_node xm_apps = {
	.id = SHIKRA_MASTER_AMPSS_M0,
	.name = "xm_apps",
	.buswidth = 16,
	.qos.ap_owned = true,
	.qos.qos_port = 5,
	.qos.urg_fwd_en = false,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.mas_rpm_id = 0,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(xm_apps_links),
	.links = xm_apps_links,
};

static const u16 xm_tcu_links[] = {
	SHIKRA_SLAVE_LLCC,
	SHIKRA_SLAVE_MEMNOC_SNOC,
};

static struct qcom_icc_node xm_tcu = {
	.id = SHIKRA_MASTER_SYS_TCU,
	.name = "xm_tcu",
	.buswidth = 8,
	.qos.ap_owned = true,
	.qos.qos_port = 2,
	.qos.urg_fwd_en = false,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 6,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(xm_tcu_links),
	.links = xm_tcu_links,
};

static const u16 qnm_camera_nrt_links[] = {
	SHIKRA_SLAVE_MMNRT_VIRT,
};

static struct qcom_icc_node qnm_camera_nrt = {
	.id = SHIKRA_MASTER_CAMNOC_SF,
	.name = "qnm_camera_nrt",
	.buswidth = 32,
	.qos.ap_owned = true,
	.qos.qos_port = 3,
	.qos.urg_fwd_en = false,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 3,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qnm_camera_nrt_links),
	.links = qnm_camera_nrt_links,
};

static const u16 qxm_venus0_links[] = {
	SHIKRA_SLAVE_MMNRT_VIRT,
};

static struct qcom_icc_node qxm_venus0 = {
	.id = SHIKRA_MASTER_VIDEO_P0,
	.name = "qxm_venus0",
	.buswidth = 16,
	.qos.ap_owned = true,
	.qos.qos_port = 8,
	.qos.urg_fwd_en = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qxm_venus0_links),
	.links = qxm_venus0_links,
};

static const u16 qxm_venus_cpu_links[] = {
	SHIKRA_SLAVE_MMNRT_VIRT,
};

static struct qcom_icc_node qxm_venus_cpu = {
	.id = SHIKRA_MASTER_VIDEO_PROC,
	.name = "qxm_venus_cpu",
	.buswidth = 8,
	.qos.ap_owned = true,
	.qos.qos_port = 12,
	.qos.urg_fwd_en = false,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qxm_venus_cpu_links),
	.links = qxm_venus_cpu_links,
};

static const u16 qnm_camera_rt_links[] = {
	SHIKRA_SLAVE_MM_MEMNOC,
};

static struct qcom_icc_node qnm_camera_rt = {
	.id = SHIKRA_MASTER_CAMNOC_HF,
	.name = "qnm_camera_rt",
	.buswidth = 32,
	.qos.ap_owned = true,
	.qos.qos_port = 9,
	.qos.urg_fwd_en = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qnm_camera_rt_links),
	.links = qnm_camera_rt_links,
};

static const u16 qxm_mdp0_links[] = {
	SHIKRA_SLAVE_MM_MEMNOC,
};

static struct qcom_icc_node qxm_mdp0 = {
	.id = SHIKRA_MASTER_MDP_PORT0,
	.name = "qxm_mdp0",
	.buswidth = 16,
	.qos.ap_owned = true,
	.qos.qos_port = 4,
	.qos.urg_fwd_en = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qxm_mdp0_links),
	.links = qxm_mdp0_links,
};

static const u16 mmrt_virt_master_links[] = {
	SHIKRA_SLAVE_MM_MEMNOC,
};

static struct qcom_icc_node mmrt_virt_master = {
	.id = SHIKRA_MASTER_MMRT_VIRT,
	.name = "mmrt_virt_master",
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mmrt_virt_master_links),
	.links = mmrt_virt_master_links,
};

static const u16 qhm_snoc_cfg_links[] = {
	SHIKRA_SLAVE_SERVICE_SNOC,
};

static struct qcom_icc_node qhm_snoc_cfg = {
	.id = SHIKRA_MASTER_SNOC_CFG,
	.name = "qhm_snoc_cfg",
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qhm_snoc_cfg_links),
	.links = qhm_snoc_cfg_links,
};

static const u16 qhm_tic_links[] = {
	SHIKRA_SLAVE_APPSS,
	SHIKRA_SLAVE_MCUSS,
	SHIKRA_SLAVE_WCSS,
	SHIKRA_SLAVE_MEMNOC_SF,
	SHIKRA_SNOC_CNOC_SLV,
	SHIKRA_SLAVE_BOOTIMEM,
	SHIKRA_SLAVE_OCIMEM,
	SHIKRA_SLAVE_PIMEM,
	SHIKRA_SLAVE_QDSS_STM,
	SHIKRA_SLAVE_TCU,
};

static struct qcom_icc_node qhm_tic = {
	.id = SHIKRA_MASTER_TIC,
	.name = "qhm_tic",
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qhm_tic_links),
	.links = qhm_tic_links,
};

static const u16 qnm_anoc_snoc_links[] = {
	SHIKRA_SLAVE_MEMNOC_SF,
};

static struct qcom_icc_node qnm_anoc_snoc = {
	.id = SHIKRA_MASTER_ANOC_SNOC,
	.name = "qnm_anoc_snoc",
	.buswidth = 16,
	.mas_rpm_id = 110,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qnm_anoc_snoc_links),
	.links = qnm_anoc_snoc_links,
};

static const u16 qnm_memnoc_pcie_links[] = {
	SHIKRA_SLAVE_PCIE2_0,
};

static struct qcom_icc_node qnm_memnoc_pcie = {
	.id = SHIKRA_MASTER_MEMNOC_PCIE,
	.name = "qnm_memnoc_pcie",
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qnm_memnoc_pcie_links),
	.links = qnm_memnoc_pcie_links,
};

static const u16 qnm_memnoc_snoc_links[] = {
	SHIKRA_SLAVE_APPSS,
	SHIKRA_SLAVE_MCUSS,
	SHIKRA_SLAVE_WCSS,
	SHIKRA_SNOC_CNOC_SLV,
	SHIKRA_SLAVE_BOOTIMEM,
	SHIKRA_SLAVE_OCIMEM,
	SHIKRA_SLAVE_PIMEM,
	SHIKRA_SLAVE_QDSS_STM,
	SHIKRA_SLAVE_TCU,
};

static struct qcom_icc_node qnm_memnoc_snoc = {
	.id = SHIKRA_MASTER_MEMNOC_SNOC,
	.name = "qnm_memnoc_snoc",
	.buswidth = 8,
	.mas_rpm_id = 184,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qnm_memnoc_snoc_links),
	.links = qnm_memnoc_snoc_links,
};

static const u16 qxm_pimem_links[] = {
	SHIKRA_SLAVE_MEMNOC_SF,
	SHIKRA_SLAVE_OCIMEM,
};

static struct qcom_icc_node qxm_pimem = {
	.id = SHIKRA_MASTER_PIMEM,
	.name = "qxm_pimem",
	.buswidth = 8,
	.qos.ap_owned = true,
	.qos.qos_port = 14,
	.qos.urg_fwd_en = false,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qxm_pimem_links),
	.links = qxm_pimem_links,
};

static const u16 xm_pcie2_0_links[] = {
	SHIKRA_SLAVE_PCIE_MEMNOC,
};

static struct qcom_icc_node xm_pcie2_0 = {
	.id = SHIKRA_MASTER_PCIE2_0,
	.name = "xm_pcie2_0",
	.buswidth = 8,
	.qos.ap_owned = true,
	.qos.qos_port = 21,
	.qos.urg_fwd_en = false,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = 186,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(xm_pcie2_0_links),
	.links = xm_pcie2_0_links,
};

static const u16 qhm_qdss_bam_links[] = {
	SHIKRA_SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node qhm_qdss_bam = {
	.id = SHIKRA_MASTER_QDSS_BAM,
	.name = "qhm_qdss_bam",
	.buswidth = 4,
	.qos.ap_owned = true,
	.qos.qos_port = 2,
	.qos.urg_fwd_en = false,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qhm_qdss_bam_links),
	.links = qhm_qdss_bam_links,
};

static const u16 qhm_qpic_links[] = {
	SHIKRA_SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node qhm_qpic = {
	.id = SHIKRA_MASTER_QPIC,
	.name = "qhm_qpic",
	.buswidth = 4,
	.qos.ap_owned = true,
	.qos.qos_port = 1,
	.qos.urg_fwd_en = false,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qhm_qpic_links),
	.links = qhm_qpic_links,
};

static const u16 qhm_qup0_links[] = {
	SHIKRA_SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node qhm_qup0 = {
	.id = SHIKRA_MASTER_QUP_0,
	.name = "qhm_qup0",
	.buswidth = 4,
	.qos.ap_owned = true,
	.qos.qos_port = 0,
	.qos.urg_fwd_en = false,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = 166,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qhm_qup0_links),
	.links = qhm_qup0_links,
};

static const u16 qnm_cnoc_snoc_links[] = {
	SHIKRA_SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node qnm_cnoc_snoc = {
	.id = SHIKRA_CNOC_SNOC_MAS,
	.name = "qnm_cnoc_snoc",
	.buswidth = 4,
	.qos.ap_owned = true,
	.qos.qos_port = 7,
	.qos.urg_fwd_en = false,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qnm_cnoc_snoc_links),
	.links = qnm_cnoc_snoc_links,
};

static const u16 qxm_audio_links[] = {
	SHIKRA_SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node qxm_audio = {
	.id = SHIKRA_MASTER_AUDIO,
	.name = "qxm_audio",
	.buswidth = 8,
	.qos.ap_owned = true,
	.qos.qos_port = 22,
	.qos.urg_fwd_en = false,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 3,
	.mas_rpm_id = 78,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qxm_audio_links),
	.links = qxm_audio_links,
};

static const u16 xm_emac_0_links[] = {
	SHIKRA_SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node xm_emac_0 = {
	.id = SHIKRA_MASTER_EMAC_0,
	.name = "xm_emac_0",
	.buswidth = 8,
	.qos.ap_owned = true,
	.qos.qos_port = 19,
	.qos.urg_fwd_en = false,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(xm_emac_0_links),
	.links = xm_emac_0_links,
};

static const u16 xm_emac_1_links[] = {
	SHIKRA_SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node xm_emac_1 = {
	.id = SHIKRA_MASTER_EMAC_1,
	.name = "xm_emac_1",
	.buswidth = 8,
	.qos.ap_owned = true,
	.qos.qos_port = 20,
	.qos.urg_fwd_en = false,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(xm_emac_1_links),
	.links = xm_emac_1_links,
};

static const u16 xm_qdss_etr_links[] = {
	SHIKRA_SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node xm_qdss_etr = {
	.id = SHIKRA_MASTER_QDSS_ETR,
	.name = "xm_qdss_etr",
	.buswidth = 8,
	.qos.ap_owned = true,
	.qos.qos_port = 11,
	.qos.urg_fwd_en = false,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(xm_qdss_etr_links),
	.links = xm_qdss_etr_links,
};

static const u16 xm_sdc1_links[] = {
	SHIKRA_SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node xm_sdc1 = {
	.id = SHIKRA_MASTER_SDCC_1,
	.name = "xm_sdc1",
	.buswidth = 8,
	.qos.ap_owned = true,
	.qos.qos_port = 13,
	.qos.urg_fwd_en = false,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(xm_sdc1_links),
	.links = xm_sdc1_links,
};

static const u16 xm_sdc2_links[] = {
	SHIKRA_SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node xm_sdc2 = {
	.id = SHIKRA_MASTER_SDCC_2,
	.name = "xm_sdc2",
	.buswidth = 8,
	.qos.ap_owned = true,
	.qos.qos_port = 17,
	.qos.urg_fwd_en = false,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(xm_sdc2_links),
	.links = xm_sdc2_links,
};

static const u16 xm_usb2_0_links[] = {
	SHIKRA_SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node xm_usb2_0 = {
	.id = SHIKRA_MASTER_USB2_0,
	.name = "xm_usb2_0",
	.buswidth = 8,
	.qos.ap_owned = true,
	.qos.qos_port = 24,
	.qos.urg_fwd_en = false,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(xm_usb2_0_links),
	.links = xm_usb2_0_links,
};

static const u16 xm_usb3_0_links[] = {
	SHIKRA_SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node xm_usb3_0 = {
	.id = SHIKRA_MASTER_USB3,
	.name = "xm_usb3_0",
	.buswidth = 8,
	.qos.ap_owned = true,
	.qos.qos_port = 18,
	.qos.urg_fwd_en = false,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(xm_usb3_0_links),
	.links = xm_usb3_0_links,
};

static const u16 crypto_c0_links[] = {
	SHIKRA_SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node crypto_c0 = {
	.id = SHIKRA_MASTER_CRYPTO_CORE0,
	.name = "crypto_c0",
	.buswidth = 8,
	.qos.ap_owned = true,
	.qos.qos_port = 16,
	.qos.urg_fwd_en = false,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = 23,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(crypto_c0_links),
	.links = crypto_c0_links,
};

/* Slave nodes */
static struct qcom_icc_node qup0_core_slave = {
	.id = SHIKRA_SLAVE_QUP_CORE_0,
	.name = "qup0_core_slave",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 264,
};

static struct qcom_icc_node qhs_ahb2phy_usb = {
	.id = SHIKRA_SLAVE_AHB2PHY_USB,
	.name = "qhs_ahb2phy_usb",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_apss_throttle_cfg = {
	.id = SHIKRA_SLAVE_APSS_THROTTLE_CFG,
	.name = "qhs_apss_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_audio = {
	.id = SHIKRA_SLAVE_AUDIO,
	.name = "qhs_audio",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_boot_rom = {
	.id = SHIKRA_SLAVE_BOOT_ROM,
	.name = "qhs_boot_rom",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_camera_nrt_throttle_cfg = {
	.id = SHIKRA_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	.name = "qhs_camera_nrt_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_camera_ss_cfg = {
	.id = SHIKRA_SLAVE_CAMERA_CFG,
	.name = "qhs_camera_ss_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_cdsp_throttle_cfg = {
	.id = SHIKRA_SLAVE_CDSP_THROTTLE_CFG,
	.name = "qhs_cdsp_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_clk_ctl = {
	.id = SHIKRA_SLAVE_CLK_CTL,
	.name = "qhs_clk_ctl",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_compute_dsp_cfg = {
	.id = SHIKRA_SLAVE_DSP_CFG,
	.name = "qhs_compute_dsp_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_cpr_cx = {
	.id = SHIKRA_SLAVE_RBCPR_CX_CFG,
	.name = "qhs_cpr_cx",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_cpr_mx = {
	.id = SHIKRA_SLAVE_RBCPR_MX_CFG,
	.name = "qhs_cpr_mx",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_crypto0_cfg = {
	.id = SHIKRA_SLAVE_CRYPTO_0_CFG,
	.name = "qhs_crypto0_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_ddr_ss_cfg = {
	.id = SHIKRA_SLAVE_DDR_SS_CFG,
	.name = "qhs_ddr_ss_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_disp_ss_cfg = {
	.id = SHIKRA_SLAVE_DISPLAY_CFG,
	.name = "qhs_disp_ss_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_emac0_cfg = {
	.id = SHIKRA_SLAVE_EMAC0_CFG,
	.name = "qhs_emac0_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_emac1_cfg = {
	.id = SHIKRA_SLAVE_EMAC1_CFG,
	.name = "qhs_emac1_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_gpu_cfg = {
	.id = SHIKRA_SLAVE_GPU_CFG,
	.name = "qhs_gpu_cfg",
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_gpu_throttle_cfg = {
	.id = SHIKRA_SLAVE_GPU_THROTTLE_CFG,
	.name = "qhs_gpu_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_hwkm = {
	.id = SHIKRA_SLAVE_HWKM,
	.name = "qhs_hwkm",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_imem_cfg = {
	.id = SHIKRA_SLAVE_IMEM_CFG,
	.name = "qhs_imem_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_mapss = {
	.id = SHIKRA_SLAVE_MAPSS,
	.name = "qhs_mapss",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_mdsp_mpu_cfg = {
	.id = SHIKRA_SLAVE_MDSP_MPU_CFG,
	.name = "qhs_mdsp_mpu_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_mesg_ram = {
	.id = SHIKRA_SLAVE_MESSAGE_RAM,
	.name = "qhs_mesg_ram",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_mss = {
	.id = SHIKRA_SLAVE_MSS,
	.name = "qhs_mss",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_pcie_cfg = {
	.id = SHIKRA_SLAVE_PCIE_CFG,
	.name = "qhs_pcie_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_pdm = {
	.id = SHIKRA_SLAVE_PDM,
	.name = "qhs_pdm",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_pimem_cfg = {
	.id = SHIKRA_SLAVE_PIMEM_CFG,
	.name = "qhs_pimem_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_pka_wrapper = {
	.id = SHIKRA_SLAVE_PKA_WRAPPER_CFG,
	.name = "qhs_pka_wrapper",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_pmic_arb = {
	.id = SHIKRA_SLAVE_PMIC_ARB,
	.name = "qhs_pmic_arb",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_qdss_cfg = {
	.id = SHIKRA_SLAVE_QDSS_CFG,
	.name = "qhs_qdss_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_qm_cfg = {
	.id = SHIKRA_SLAVE_QM_CFG,
	.name = "qhs_qm_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_qm_mpu_cfg = {
	.id = SHIKRA_SLAVE_QM_MPU_CFG,
	.name = "qhs_qm_mpu_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_qpic = {
	.id = SHIKRA_SLAVE_QPIC,
	.name = "qhs_qpic",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_qup0 = {
	.id = SHIKRA_SLAVE_QUP_0,
	.name = "qhs_qup0",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_rpm = {
	.id = SHIKRA_SLAVE_RPM,
	.name = "qhs_rpm",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_sdc1 = {
	.id = SHIKRA_SLAVE_SDCC_1,
	.name = "qhs_sdc1",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_sdc2 = {
	.id = SHIKRA_SLAVE_SDCC_2,
	.name = "qhs_sdc2",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_security = {
	.id = SHIKRA_SLAVE_SECURITY,
	.name = "qhs_security",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static const u16 qhs_snoc_cfg_links[] = {
	SHIKRA_MASTER_SNOC_CFG,
};

static struct qcom_icc_node qhs_snoc_cfg = {
	.id = SHIKRA_SLAVE_SNOC_CFG,
	.name = "qhs_snoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = qhs_snoc_cfg_links,
};

static struct qcom_icc_node qhs_snoc_sf_throttle_cfg = {
	.id = SHIKRA_SNOC_SF_THROTTLE_CFG,
	.name = "qhs_snoc_sf_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_tlmm = {
	.id = SHIKRA_SLAVE_TLMM,
	.name = "qhs_tlmm",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_tscss = {
	.id = SHIKRA_SLAVE_TSCSS,
	.name = "qhs_tscss",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_usb2 = {
	.id = SHIKRA_SLAVE_USB2,
	.name = "qhs_usb2",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_usb3 = {
	.id = SHIKRA_SLAVE_USB3,
	.name = "qhs_usb3",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_venus_cfg = {
	.id = SHIKRA_SLAVE_VENUS_CFG,
	.name = "qhs_venus_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_venus_throttle_cfg = {
	.id = SHIKRA_SLAVE_VENUS_THROTTLE_CFG,
	.name = "qhs_venus_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_vsense_ctrl_cfg = {
	.id = SHIKRA_SLAVE_VSENSE_CTRL_CFG,
	.name = "qhs_vsense_ctrl_cfg",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node srvc_cnoc = {
	.id = SHIKRA_SLAVE_SERVICE_CNOC,
	.name = "srvc_cnoc",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node ebi = {
	.id = SHIKRA_SLAVE_EBI_CH0,
	.name = "ebi",
	.channels = 2,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 0,
};

static const u16 qns_llcc_links[] = {
	SHIKRA_MASTER_LLCC,
};

static struct qcom_icc_node qns_llcc = {
	.id = SHIKRA_SLAVE_LLCC,
	.name = "qns_llcc",
	.channels = 2,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 312,
	.num_links = 1,
	.links = qns_llcc_links,
};

static const u16 qns_memnoc_snoc_links[] = {
	SHIKRA_MASTER_MEMNOC_SNOC,
};

static struct qcom_icc_node qns_memnoc_snoc = {
	.id = SHIKRA_SLAVE_MEMNOC_SNOC,
	.name = "qns_memnoc_snoc",
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 314,
	.num_links = 1,
	.links = qns_memnoc_snoc_links,
};

static const u16 qns_pcie_links[] = {
	SHIKRA_MASTER_MEMNOC_PCIE,
};

static struct qcom_icc_node qns_pcie = {
	.id = SHIKRA_SLAVE_MEM_NOC_PCIE_SNOC,
	.name = "qns_pcie",
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = qns_pcie_links,
};

static const u16 mmnrt_virt_slave_links[] = {
	SHIKRA_MASTER_MMRT_VIRT,
};

static struct qcom_icc_node mmnrt_virt_slave = {
	.id = SHIKRA_SLAVE_MMNRT_VIRT,
	.name = "mmnrt_virt_slave",
	.channels = 1,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = mmnrt_virt_slave_links,
};

static const u16 qns_mm_memnoc_links[] = {
	SHIKRA_MASTER_MNOC_HF_MEM_NOC,
};

static struct qcom_icc_node qns_mm_memnoc = {
	.id = SHIKRA_SLAVE_MM_MEMNOC,
	.name = "qns_mm_memnoc",
	.channels = 1,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = qns_mm_memnoc_links,
};

static struct qcom_icc_node qhs_apss = {
	.id = SHIKRA_SLAVE_APPSS,
	.name = "qhs_apss",
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_mcuss = {
	.id = SHIKRA_SLAVE_MCUSS,
	.name = "qhs_mcuss",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 319,
};

static struct qcom_icc_node qhs_wcss = {
	.id = SHIKRA_SLAVE_WCSS,
	.name = "qhs_wcss",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 23,
};

static const u16 qns_memnoc_sf_links[] = {
	SHIKRA_MASTER_SNOC_SF_MEM_NOC,
};

static struct qcom_icc_node qns_memnoc_sf = {
	.id = SHIKRA_SLAVE_MEMNOC_SF,
	.name = "qns_memnoc_sf",
	.channels = 1,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 313,
	.num_links = 1,
	.links = qns_memnoc_sf_links,
};

static const u16 qns_snoc_cnoc_links[] = {
	SHIKRA_SNOC_CNOC_MAS,
};

static struct qcom_icc_node qns_snoc_cnoc = {
	.id = SHIKRA_SNOC_CNOC_SLV,
	.name = "qns_snoc_cnoc",
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 25,
	.num_links = 1,
	.links = qns_snoc_cnoc_links,
};

static struct qcom_icc_node qxs_bootimem = {
	.id = SHIKRA_SLAVE_BOOTIMEM,
	.name = "qxs_bootimem",
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qxs_imem = {
	.id = SHIKRA_SLAVE_OCIMEM,
	.name = "qxs_imem",
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 26,
};

static struct qcom_icc_node qxs_pimem = {
	.id = SHIKRA_SLAVE_PIMEM,
	.name = "qxs_pimem",
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node srvc_snoc = {
	.id = SHIKRA_SLAVE_SERVICE_SNOC,
	.name = "srvc_snoc",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node xs_pcie2_0 = {
	.id = SHIKRA_SLAVE_PCIE2_0,
	.name = "xs_pcie2_0",
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node xs_qdss_stm = {
	.id = SHIKRA_SLAVE_QDSS_STM,
	.name = "xs_qdss_stm",
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 30,
};

static struct qcom_icc_node xs_sys_tcu_cfg = {
	.id = SHIKRA_SLAVE_TCU,
	.name = "xs_sys_tcu_cfg",
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static const u16 qns_pcie_memnoc_links[] = {
	SHIKRA_MASTER_ANOC_PCIE_MEM_NOC,
};

static struct qcom_icc_node qns_pcie_memnoc = {
	.id = SHIKRA_SLAVE_PCIE_MEMNOC,
	.name = "qns_pcie_memnoc",
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 317,
	.num_links = 1,
	.links = qns_pcie_memnoc_links,
};

static const u16 qns_anoc_snoc_links[] = {
	SHIKRA_MASTER_ANOC_SNOC,
};

static struct qcom_icc_node qns_anoc_snoc = {
	.id = SHIKRA_SLAVE_ANOC_SNOC,
	.name = "qns_anoc_snoc",
	.channels = 1,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 141,
	.num_links = 1,
	.links = qns_anoc_snoc_links,
};

/* NoC descriptors */
static struct qcom_icc_node * const shikra_clk_virt_nodes[] = {
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
};

static const struct qcom_icc_desc shikra_clk_virt = {
	.type = QCOM_ICC_QNOC,
	.nodes = shikra_clk_virt_nodes,
	.num_nodes = ARRAY_SIZE(shikra_clk_virt_nodes),
	.bus_clk_desc = &qup_clk,
	.keep_alive = true,
};

static struct qcom_icc_node * const shikra_config_noc_nodes[] = {
	[SNOC_CNOC_MAS] = &qnm_snoc_cnoc,
	[MASTER_QDSS_DAP] = &xm_dap,
	[SLAVE_AHB2PHY_USB] = &qhs_ahb2phy_usb,
	[SLAVE_APSS_THROTTLE_CFG] = &qhs_apss_throttle_cfg,
	[SLAVE_AUDIO] = &qhs_audio,
	[SLAVE_BOOT_ROM] = &qhs_boot_rom,
	[SLAVE_CAMERA_NRT_THROTTLE_CFG] = &qhs_camera_nrt_throttle_cfg,
	[SLAVE_CAMERA_CFG] = &qhs_camera_ss_cfg,
	[SLAVE_CDSP_THROTTLE_CFG] = &qhs_cdsp_throttle_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_DSP_CFG] = &qhs_compute_dsp_cfg,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MX_CFG] = &qhs_cpr_mx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_DDR_SS_CFG] = &qhs_ddr_ss_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_disp_ss_cfg,
	[SLAVE_EMAC0_CFG] = &qhs_emac0_cfg,
	[SLAVE_EMAC1_CFG] = &qhs_emac1_cfg,
	[SLAVE_GPU_CFG] = &qhs_gpu_cfg,
	[SLAVE_GPU_THROTTLE_CFG] = &qhs_gpu_throttle_cfg,
	[SLAVE_HWKM] = &qhs_hwkm,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_MAPSS] = &qhs_mapss,
	[SLAVE_MDSP_MPU_CFG] = &qhs_mdsp_mpu_cfg,
	[SLAVE_MESSAGE_RAM] = &qhs_mesg_ram,
	[SLAVE_MSS] = &qhs_mss,
	[SLAVE_PCIE_CFG] = &qhs_pcie_cfg,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PKA_WRAPPER_CFG] = &qhs_pka_wrapper,
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
	[SNOC_SF_THROTTLE_CFG] = &qhs_snoc_sf_throttle_cfg,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_TSCSS] = &qhs_tscss,
	[SLAVE_USB2] = &qhs_usb2,
	[SLAVE_USB3] = &qhs_usb3,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VENUS_THROTTLE_CFG] = &qhs_venus_throttle_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_SERVICE_CNOC] = &srvc_cnoc,
};

static const struct regmap_config shikra_config_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x8080,
	.fast_io = true,
};

static const struct qcom_icc_desc shikra_config_noc = {
	.type = QCOM_ICC_QNOC,
	.nodes = shikra_config_noc_nodes,
	.num_nodes = ARRAY_SIZE(shikra_config_noc_nodes),
	.bus_clk_desc = &bus_1_clk,
	.regmap_cfg = &shikra_config_noc_regmap_config,
	.keep_alive = true,
};

static struct qcom_icc_node * const shikra_mc_virt_nodes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI_CH0] = &ebi,
};

static const struct qcom_icc_desc shikra_mc_virt = {
	.type = QCOM_ICC_QNOC,
	.nodes = shikra_mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(shikra_mc_virt_nodes),
	.bus_clk_desc = &bimc_clk,
	.keep_alive = true,
	.ab_coeff = 152,
};

static struct qcom_icc_node * const shikra_mem_noc_core_nodes[] = {
	[MASTER_GRAPHICS_3D] = &qnm_gpu,
	[MASTER_MNOC_HF_MEM_NOC] = &qnm_mnoc_hf,
	[MASTER_ANOC_PCIE_MEM_NOC] = &qnm_pcie,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[MASTER_AMPSS_M0] = &xm_apps,
	[MASTER_SYS_TCU] = &xm_tcu,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_MEMNOC_SNOC] = &qns_memnoc_snoc,
	[SLAVE_MEM_NOC_PCIE_SNOC] = &qns_pcie,
};

static const struct regmap_config shikra_mem_noc_core_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x43080,
	.fast_io = true,
};

static const struct qcom_icc_desc shikra_mem_noc_core = {
	.type = QCOM_ICC_QNOC,
	.nodes = shikra_mem_noc_core_nodes,
	.num_nodes = ARRAY_SIZE(shikra_mem_noc_core_nodes),
	.bus_clk_desc = &mem_1_clk,
	.regmap_cfg = &shikra_mem_noc_core_regmap_config,
	.intf_clocks = memnoc_intf_clocks,
	.num_intf_clocks = ARRAY_SIZE(memnoc_intf_clocks),
	.qos_offset = 0x28000,
	.keep_alive = true,
	.ab_coeff = 142,
};

static struct qcom_icc_node * const shikra_mmnrt_virt_nodes[] = {
	[MASTER_CAMNOC_SF] = &qnm_camera_nrt,
	[MASTER_VIDEO_P0] = &qxm_venus0,
	[MASTER_VIDEO_PROC] = &qxm_venus_cpu,
	[SLAVE_MMNRT_VIRT] = &mmnrt_virt_slave,
};

static const struct regmap_config shikra_sys_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x6a080,
	.fast_io = true,
};

static const struct qcom_icc_desc shikra_mmnrt_virt = {
	.type = QCOM_ICC_QNOC,
	.nodes = shikra_mmnrt_virt_nodes,
	.num_nodes = ARRAY_SIZE(shikra_mmnrt_virt_nodes),
	.bus_clk_desc = &mmaxi_0_clk,
	.regmap_cfg = &shikra_sys_noc_regmap_config,
	.qos_offset = 0x51000,
	.keep_alive = true,
	.ab_coeff = 142,
};

static struct qcom_icc_node * const shikra_mmrt_virt_nodes[] = {
	[MASTER_CAMNOC_HF] = &qnm_camera_rt,
	[MASTER_MDP_PORT0] = &qxm_mdp0,
	[MASTER_MMRT_VIRT] = &mmrt_virt_master,
	[SLAVE_MM_MEMNOC] = &qns_mm_memnoc,
};

static const struct qcom_icc_desc shikra_mmrt_virt = {
	.type = QCOM_ICC_QNOC,
	.nodes = shikra_mmrt_virt_nodes,
	.num_nodes = ARRAY_SIZE(shikra_mmrt_virt_nodes),
	.bus_clk_desc = &mmaxi_1_clk,
	.regmap_cfg = &shikra_sys_noc_regmap_config,
	.qos_offset = 0x51000,
	.keep_alive = true,
	.ab_coeff = 142,
};

static struct qcom_icc_node * const shikra_sys_noc_nodes[] = {
	[MASTER_SNOC_CFG] = &qhm_snoc_cfg,
	[MASTER_TIC] = &qhm_tic,
	[MASTER_ANOC_SNOC] = &qnm_anoc_snoc,
	[MASTER_MEMNOC_PCIE] = &qnm_memnoc_pcie,
	[MASTER_MEMNOC_SNOC] = &qnm_memnoc_snoc,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_PCIE2_0] = &xm_pcie2_0,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QPIC] = &qhm_qpic,
	[MASTER_QUP_0] = &qhm_qup0,
	[CNOC_SNOC_MAS] = &qnm_cnoc_snoc,
	[MASTER_AUDIO] = &qxm_audio,
	[MASTER_EMAC_0] = &xm_emac_0,
	[MASTER_EMAC_1] = &xm_emac_1,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[MASTER_SDCC_1] = &xm_sdc1,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_USB2_0] = &xm_usb2_0,
	[MASTER_USB3] = &xm_usb3_0,
	[MASTER_CRYPTO_CORE0] = &crypto_c0,
	[SLAVE_APPSS] = &qhs_apss,
	[SLAVE_MCUSS] = &qhs_mcuss,
	[SLAVE_WCSS] = &qhs_wcss,
	[SLAVE_MEMNOC_SF] = &qns_memnoc_sf,
	[SNOC_CNOC_SLV] = &qns_snoc_cnoc,
	[SLAVE_BOOTIMEM] = &qxs_bootimem,
	[SLAVE_OCIMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SERVICE_SNOC] = &srvc_snoc,
	[SLAVE_PCIE2_0] = &xs_pcie2_0,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
	[SLAVE_PCIE_MEMNOC] = &qns_pcie_memnoc,
	[SLAVE_ANOC_SNOC] = &qns_anoc_snoc,
};

static const struct qcom_icc_desc shikra_sys_noc = {
	.type = QCOM_ICC_QNOC,
	.nodes = shikra_sys_noc_nodes,
	.num_nodes = ARRAY_SIZE(shikra_sys_noc_nodes),
	.bus_clk_desc = &bus_2_clk,
	.regmap_cfg = &shikra_sys_noc_regmap_config,
	.intf_clocks = sys_noc_intf_clocks,
	.num_intf_clocks = ARRAY_SIZE(sys_noc_intf_clocks),
	.qos_offset = 0x51000,
	.keep_alive = true,
};

static const struct of_device_id shikra_qnoc_of_match[] = {
	{ .compatible = "qcom,shikra-clk-virt", .data = &shikra_clk_virt },
	{ .compatible = "qcom,shikra-config-noc", .data = &shikra_config_noc },
	{ .compatible = "qcom,shikra-mc-virt", .data = &shikra_mc_virt },
	{ .compatible = "qcom,shikra-mem-noc-core", .data = &shikra_mem_noc_core },
	{ .compatible = "qcom,shikra-mmnrt-virt", .data = &shikra_mmnrt_virt },
	{ .compatible = "qcom,shikra-mmrt-virt", .data = &shikra_mmrt_virt },
	{ .compatible = "qcom,shikra-sys-noc", .data = &shikra_sys_noc },
	{ },
};
MODULE_DEVICE_TABLE(of, shikra_qnoc_of_match);

static struct platform_driver shikra_qnoc_driver = {
	.probe = qnoc_probe,
	.remove = qnoc_remove,
	.driver = {
		.name = "qnoc-shikra",
		.of_match_table = shikra_qnoc_of_match,
		.sync_state = icc_sync_state,
	},
};

static int __init qnoc_driver_init(void)
{
	return platform_driver_register(&shikra_qnoc_driver);
}
core_initcall(qnoc_driver_init);

static void __exit qnoc_driver_exit(void)
{
	platform_driver_unregister(&shikra_qnoc_driver);
}
module_exit(qnoc_driver_exit);

MODULE_DESCRIPTION("Qualcomm Shikra NoC driver");
MODULE_LICENSE("GPL");
