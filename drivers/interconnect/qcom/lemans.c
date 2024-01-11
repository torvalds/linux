// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <asm/div64.h>
#include <dt-bindings/interconnect/qcom,lemans.h>
#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sort.h>

#include "icc-rpmh.h"
#include "qnoc-qos.h"

enum {
	VOTER_IDX_HLOS,
};

static const struct regmap_config icc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
};

static struct qcom_icc_qosbox qxm_qup3_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x11000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node qxm_qup3 = {
	.name = "qxm_qup3",
	.id = MASTER_QUP_3,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qxm_qup3_qos,
	.num_links = 1,
	.links = { SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_qosbox xm_emac_0_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x12000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node xm_emac_0 = {
	.name = "xm_emac_0",
	.id = MASTER_EMAC,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &xm_emac_0_qos,
	.num_links = 1,
	.links = { SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_qosbox xm_emac_1_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x13000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node xm_emac_1 = {
	.name = "xm_emac_1",
	.id = MASTER_EMAC_1,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &xm_emac_1_qos,
	.num_links = 1,
	.links = { SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_qosbox xm_sdc1_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x14000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node xm_sdc1 = {
	.name = "xm_sdc1",
	.id = MASTER_SDC,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &xm_sdc1_qos,
	.num_links = 1,
	.links = { SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_qosbox xm_ufs_mem_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x15000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.id = MASTER_UFS_MEM,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &xm_ufs_mem_qos,
	.num_links = 1,
	.links = { SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_qosbox xm_usb2_2_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x16000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node xm_usb2_2 = {
	.name = "xm_usb2_2",
	.id = MASTER_USB2,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &xm_usb2_2_qos,
	.num_links = 1,
	.links = { SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_qosbox xm_usb3_0_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x17000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node xm_usb3_0 = {
	.name = "xm_usb3_0",
	.id = MASTER_USB3_0,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &xm_usb3_0_qos,
	.num_links = 1,
	.links = { SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_qosbox xm_usb3_1_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x18000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node xm_usb3_1 = {
	.name = "xm_usb3_1",
	.id = MASTER_USB3_1,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &xm_usb3_1_qos,
	.num_links = 1,
	.links = { SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_qosbox qhm_qdss_bam_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x14000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qhm_qdss_bam_qos,
	.num_links = 1,
	.links = { SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_qosbox qhm_qup0_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x17000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node qhm_qup0 = {
	.name = "qhm_qup0",
	.id = MASTER_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qhm_qup0_qos,
	.num_links = 1,
	.links = { SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_qosbox qhm_qup1_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x12000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node qhm_qup1 = {
	.name = "qhm_qup1",
	.id = MASTER_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qhm_qup1_qos,
	.num_links = 1,
	.links = { SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_qosbox qhm_qup2_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x15000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node qhm_qup2 = {
	.name = "qhm_qup2",
	.id = MASTER_QUP_2,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qhm_qup2_qos,
	.num_links = 1,
	.links = { SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_qosbox qnm_cnoc_datapath_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x16000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node qnm_cnoc_datapath = {
	.name = "qnm_cnoc_datapath",
	.id = MASTER_CNOC_A2NOC,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qnm_cnoc_datapath_qos,
	.num_links = 1,
	.links = { SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_qosbox qxm_crypto_0_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x18000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node qxm_crypto_0 = {
	.name = "qxm_crypto_0",
	.id = MASTER_CRYPTO_CORE0,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qxm_crypto_0_qos,
	.num_links = 1,
	.links = { SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_qosbox qxm_crypto_1_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x1a000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node qxm_crypto_1 = {
	.name = "qxm_crypto_1",
	.id = MASTER_CRYPTO_CORE1,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qxm_crypto_1_qos,
	.num_links = 1,
	.links = { SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_qosbox qxm_ipa_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x11000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node qxm_ipa = {
	.name = "qxm_ipa",
	.id = MASTER_IPA,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qxm_ipa_qos,
	.num_links = 1,
	.links = { SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_qosbox xm_qdss_etr_0_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x13000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node xm_qdss_etr_0 = {
	.name = "xm_qdss_etr_0",
	.id = MASTER_QDSS_ETR_0,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &xm_qdss_etr_0_qos,
	.num_links = 1,
	.links = { SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_qosbox xm_qdss_etr_1_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x19000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node xm_qdss_etr_1 = {
	.name = "xm_qdss_etr_1",
	.id = MASTER_QDSS_ETR_1,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &xm_qdss_etr_1_qos,
	.num_links = 1,
	.links = { SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_qosbox xm_ufs_card_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x1b000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node xm_ufs_card = {
	.name = "xm_ufs_card",
	.id = MASTER_UFS_CARD,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &xm_ufs_card_qos,
	.num_links = 1,
	.links = { SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qup0_core_master = {
	.name = "qup0_core_master",
	.id = MASTER_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { SLAVE_QUP_CORE_0 },
};

static struct qcom_icc_node qup1_core_master = {
	.name = "qup1_core_master",
	.id = MASTER_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { SLAVE_QUP_CORE_1 },
};

static struct qcom_icc_node qup2_core_master = {
	.name = "qup2_core_master",
	.id = MASTER_QUP_CORE_2,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { SLAVE_QUP_CORE_2 },
};

static struct qcom_icc_node qup3_core_master = {
	.name = "qup3_core_master",
	.id = MASTER_QUP_CORE_3,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { SLAVE_QUP_CORE_3 },
};

static struct qcom_icc_node qnm_gemnoc_cnoc = {
	.name = "qnm_gemnoc_cnoc",
	.id = MASTER_GEM_NOC_CNOC,
	.channels = 1,
	.buswidth = 16,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 82,
	.links = { SLAVE_AHB2PHY_0, SLAVE_AHB2PHY_1,
		   SLAVE_AHB2PHY_2, SLAVE_AHB2PHY_3,
		   SLAVE_ANOC_THROTTLE_CFG, SLAVE_AOSS,
		   SLAVE_APPSS, SLAVE_BOOT_ROM,
		   SLAVE_CAMERA_CFG, SLAVE_CAMERA_NRT_THROTTLE_CFG,
		   SLAVE_CAMERA_RT_THROTTLE_CFG, SLAVE_CLK_CTL,
		   SLAVE_CDSP_CFG, SLAVE_CDSP1_CFG,
		   SLAVE_RBCPR_CX_CFG, SLAVE_RBCPR_MMCX_CFG,
		   SLAVE_RBCPR_MX_CFG, SLAVE_CPR_NSPCX,
		   SLAVE_CRYPTO_0_CFG, SLAVE_CX_RDPM,
		   SLAVE_DISPLAY_CFG, SLAVE_DISPLAY_RT_THROTTLE_CFG,
		   SLAVE_DISPLAY1_CFG, SLAVE_DISPLAY1_RT_THROTTLE_CFG,
		   SLAVE_EMAC_CFG, SLAVE_EMAC1_CFG,
		   SLAVE_GP_DSP0_CFG, SLAVE_GP_DSP1_CFG,
		   SLAVE_GPDSP0_THROTTLE_CFG, SLAVE_GPDSP1_THROTTLE_CFG,
		   SLAVE_GPU_TCU_THROTTLE_CFG, SLAVE_GFX3D_CFG,
		   SLAVE_HWKM, SLAVE_IMEM_CFG,
		   SLAVE_IPA_CFG, SLAVE_IPC_ROUTER_CFG,
		   SLAVE_LPASS, SLAVE_LPASS_THROTTLE_CFG,
		   SLAVE_MX_RDPM, SLAVE_MXC_RDPM,
		   SLAVE_PCIE_0_CFG, SLAVE_PCIE_1_CFG,
		   SLAVE_PCIE_RSC_CFG, SLAVE_PCIE_TCU_THROTTLE_CFG,
		   SLAVE_PCIE_THROTTLE_CFG, SLAVE_PDM,
		   SLAVE_PIMEM_CFG, SLAVE_PKA_WRAPPER_CFG,
		   SLAVE_QDSS_CFG, SLAVE_QM_CFG,
		   SLAVE_QM_MPU_CFG, SLAVE_QUP_0,
		   SLAVE_QUP_1, SLAVE_QUP_2,
		   SLAVE_QUP_3, SLAVE_SAIL_THROTTLE_CFG,
		   SLAVE_SDC1, SLAVE_SECURITY,
		   SLAVE_SNOC_THROTTLE_CFG, SLAVE_TCSR,
		   SLAVE_TLMM, SLAVE_TSC_CFG,
		   SLAVE_UFS_CARD_CFG, SLAVE_UFS_MEM_CFG,
		   SLAVE_USB2, SLAVE_USB3_0,
		   SLAVE_USB3_1, SLAVE_VENUS_CFG,
		   SLAVE_VENUS_CVP_THROTTLE_CFG, SLAVE_VENUS_V_CPU_THROTTLE_CFG,
		   SLAVE_VENUS_VCODEC_THROTTLE_CFG, SLAVE_DDRSS_CFG,
		   SLAVE_GPDSP_NOC_CFG, SLAVE_CNOC_MNOC_HF_CFG,
		   SLAVE_CNOC_MNOC_SF_CFG, SLAVE_PCIE_ANOC_CFG,
		   SLAVE_SNOC_CFG, SLAVE_BOOT_IMEM,
		   SLAVE_IMEM, SLAVE_PIMEM,
		   SLAVE_QDSS_STM, SLAVE_TCU },
};

static struct qcom_icc_node qnm_gemnoc_pcie = {
	.name = "qnm_gemnoc_pcie",
	.id = MASTER_GEM_NOC_PCIE_SNOC,
	.channels = 1,
	.buswidth = 16,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 2,
	.links = { SLAVE_PCIE_0, SLAVE_PCIE_1 },
};

static struct qcom_icc_node qnm_cnoc_dc_noc = {
	.name = "qnm_cnoc_dc_noc",
	.id = MASTER_CNOC_DC_NOC,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 2,
	.links = { SLAVE_LLCC_CFG, SLAVE_GEM_NOC_CFG },
};

static struct qcom_icc_qosbox alm_gpu_tcu_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0xb4000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 1,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node alm_gpu_tcu = {
	.name = "alm_gpu_tcu",
	.id = MASTER_GPU_TCU,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &alm_gpu_tcu_qos,
	.num_links = 2,
	.links = { SLAVE_GEM_NOC_CNOC, SLAVE_LLCC },
};

static struct qcom_icc_qosbox alm_pcie_tcu_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0xb5000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 3,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node alm_pcie_tcu = {
	.name = "alm_pcie_tcu",
	.id = MASTER_PCIE_TCU,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &alm_pcie_tcu_qos,
	.num_links = 2,
	.links = { SLAVE_GEM_NOC_CNOC, SLAVE_LLCC },
};

static struct qcom_icc_qosbox alm_sys_tcu_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0xb6000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 6,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.id = MASTER_SYS_TCU,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &alm_sys_tcu_qos,
	.num_links = 2,
	.links = { SLAVE_GEM_NOC_CNOC, SLAVE_LLCC },
};

static struct qcom_icc_node chm_apps = {
	.name = "chm_apps",
	.id = MASTER_APPSS_PROC,
	.channels = 4,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 3,
	.links = { SLAVE_GEM_NOC_CNOC, SLAVE_LLCC,
		   SLAVE_GEM_NOC_PCIE_CNOC },
};

static struct qcom_icc_qosbox qnm_cmpnoc0_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 2,
	.offsets = { 0xf3000, 0xf4000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 0,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node qnm_cmpnoc0 = {
	.name = "qnm_cmpnoc0",
	.id = MASTER_COMPUTE_NOC,
	.channels = 2,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qnm_cmpnoc0_qos,
	.num_links = 2,
	.links = { SLAVE_GEM_NOC_CNOC, SLAVE_LLCC },
};

static struct qcom_icc_qosbox qnm_cmpnoc1_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 2,
	.offsets = { 0xf5000, 0xf6000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 0,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node qnm_cmpnoc1 = {
	.name = "qnm_cmpnoc1",
	.id = MASTER_COMPUTE_NOC_1,
	.channels = 2,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qnm_cmpnoc1_qos,
	.num_links = 2,
	.links = { SLAVE_GEM_NOC_CNOC, SLAVE_LLCC },
};

static struct qcom_icc_node qnm_gemnoc_cfg = {
	.name = "qnm_gemnoc_cfg",
	.id = MASTER_GEM_NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 4,
	.links = { SLAVE_SERVICE_GEM_NOC_1, SLAVE_SERVICE_GEM_NOC_2,
		   SLAVE_SERVICE_GEM_NOC, SLAVE_SERVICE_GEM_NOC2 },
};

static struct qcom_icc_node qnm_gpdsp_sail = {
	.name = "qnm_gpdsp_sail",
	.id = MASTER_GPDSP_SAIL,
	.channels = 1,
	.buswidth = 16,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 2,
	.links = { SLAVE_GEM_NOC_CNOC, SLAVE_LLCC },
};

static struct qcom_icc_qosbox qnm_gpu_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 2,
	.offsets = { 0xed000, 0xee000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 0,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node qnm_gpu = {
	.name = "qnm_gpu",
	.id = MASTER_GFX3D,
	.channels = 2,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qnm_gpu_qos,
	.num_links = 2,
	.links = { SLAVE_GEM_NOC_CNOC, SLAVE_LLCC },
};

static struct qcom_icc_qosbox qnm_mnoc_hf_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 2,
	.offsets = { 0xef000, 0xf0000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 0,
		.prio = 0,
		.urg_fwd = 1,
	},
};

static struct qcom_icc_node qnm_mnoc_hf = {
	.name = "qnm_mnoc_hf",
	.id = MASTER_MNOC_HF_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qnm_mnoc_hf_qos,
	.num_links = 2,
	.links = { SLAVE_LLCC, SLAVE_GEM_NOC_PCIE_CNOC },
};

static struct qcom_icc_qosbox qnm_mnoc_sf_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 2,
	.offsets = { 0xf1000, 0xf2000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 0,
		.prio = 0,
		.urg_fwd = 1,
	},
};

static struct qcom_icc_node qnm_mnoc_sf = {
	.name = "qnm_mnoc_sf",
	.id = MASTER_MNOC_SF_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qnm_mnoc_sf_qos,
	.num_links = 3,
	.links = { SLAVE_GEM_NOC_CNOC, SLAVE_LLCC,
		   SLAVE_GEM_NOC_PCIE_CNOC },
};

static struct qcom_icc_qosbox qnm_pcie_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0xb8000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node qnm_pcie = {
	.name = "qnm_pcie",
	.id = MASTER_ANOC_PCIE_GEM_NOC,
	.channels = 1,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qnm_pcie_qos,
	.num_links = 2,
	.links = { SLAVE_GEM_NOC_CNOC, SLAVE_LLCC },
};

static struct qcom_icc_qosbox qnm_snoc_gc_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0xb9000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 0,
		.prio = 0,
		.urg_fwd = 1,
	},
};

static struct qcom_icc_node qnm_snoc_gc = {
	.name = "qnm_snoc_gc",
	.id = MASTER_SNOC_GC_MEM_NOC,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qnm_snoc_gc_qos,
	.num_links = 1,
	.links = { SLAVE_LLCC },
};

static struct qcom_icc_qosbox qnm_snoc_sf_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0xba000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 0,
		.prio = 0,
		.urg_fwd = 1,
	},
};

static struct qcom_icc_node qnm_snoc_sf = {
	.name = "qnm_snoc_sf",
	.id = MASTER_SNOC_SF_MEM_NOC,
	.channels = 1,
	.buswidth = 16,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qnm_snoc_sf_qos,
	.num_links = 3,
	.links = { SLAVE_GEM_NOC_CNOC, SLAVE_LLCC,
		   SLAVE_GEM_NOC_PCIE_CNOC },
};

static struct qcom_icc_node qxm_dsp0 = {
	.name = "qxm_dsp0",
	.id = MASTER_DSP0,
	.channels = 1,
	.buswidth = 16,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { SLAVE_GP_DSP_SAIL_NOC },
};

static struct qcom_icc_node qxm_dsp1 = {
	.name = "qxm_dsp1",
	.id = MASTER_DSP1,
	.channels = 1,
	.buswidth = 16,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { SLAVE_GP_DSP_SAIL_NOC },
};

static struct qcom_icc_node qhm_config_noc = {
	.name = "qhm_config_noc",
	.id = MASTER_CNOC_LPASS_AG_NOC,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 6,
	.links = { SLAVE_LPASS_CORE_CFG, SLAVE_LPASS_LPI_CFG,
		   SLAVE_LPASS_MPU_CFG, SLAVE_LPASS_TOP_CFG,
		   SLAVE_SERVICES_LPASS_AML_NOC, SLAVE_SERVICE_LPASS_AG_NOC },
};

static struct qcom_icc_node qxm_lpass_dsp = {
	.name = "qxm_lpass_dsp",
	.id = MASTER_LPASS_PROC,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 4,
	.links = { SLAVE_LPASS_TOP_CFG, SLAVE_LPASS_SNOC,
		   SLAVE_SERVICES_LPASS_AML_NOC, SLAVE_SERVICE_LPASS_AG_NOC },
};

static struct qcom_icc_node llcc_mc = {
	.name = "llcc_mc",
	.id = MASTER_LLCC,
	.channels = 8,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { SLAVE_EBI1 },
};

static struct qcom_icc_qosbox qnm_camnoc_hf_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0xa000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 0,
		.prio = 0,
		.urg_fwd = 1,
	},
};

static struct qcom_icc_node qnm_camnoc_hf = {
	.name = "qnm_camnoc_hf",
	.id = MASTER_CAMNOC_HF,
	.channels = 1,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qnm_camnoc_hf_qos,
	.num_links = 1,
	.links = { SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_qosbox qnm_camnoc_icp_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x2a000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 0,
		.prio = 0,
		.urg_fwd = 1,
	},
};

static struct qcom_icc_node qnm_camnoc_icp = {
	.name = "qnm_camnoc_icp",
	.id = MASTER_CAMNOC_ICP,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qnm_camnoc_icp_qos,
	.num_links = 1,
	.links = { SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_qosbox qnm_camnoc_sf_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x2a080 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 0,
		.prio = 0,
		.urg_fwd = 1,
	},
};

static struct qcom_icc_node qnm_camnoc_sf = {
	.name = "qnm_camnoc_sf",
	.id = MASTER_CAMNOC_SF,
	.channels = 1,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qnm_camnoc_sf_qos,
	.num_links = 1,
	.links = { SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_qosbox qnm_mdp0_0_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0xa080 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 0,
		.prio = 0,
		.urg_fwd = 1,
	},
};

static struct qcom_icc_node qnm_mdp0_0 = {
	.name = "qnm_mdp0_0",
	.id = MASTER_MDP0,
	.channels = 1,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qnm_mdp0_0_qos,
	.num_links = 1,
	.links = { SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_qosbox qnm_mdp0_1_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0xa180 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 0,
		.prio = 0,
		.urg_fwd = 1,
	},
};

static struct qcom_icc_node qnm_mdp0_1 = {
	.name = "qnm_mdp0_1",
	.id = MASTER_MDP1,
	.channels = 1,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qnm_mdp0_1_qos,
	.num_links = 1,
	.links = { SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_qosbox qnm_mdp1_0_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0xa100 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 0,
		.prio = 0,
		.urg_fwd = 1,
	},
};

static struct qcom_icc_node qnm_mdp1_0 = {
	.name = "qnm_mdp1_0",
	.id = MASTER_MDP_CORE1_0,
	.channels = 1,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qnm_mdp1_0_qos,
	.num_links = 1,
	.links = { SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_qosbox qnm_mdp1_1_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0xa200 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 0,
		.prio = 0,
		.urg_fwd = 1,
	},
};

static struct qcom_icc_node qnm_mdp1_1 = {
	.name = "qnm_mdp1_1",
	.id = MASTER_MDP_CORE1_1,
	.channels = 1,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qnm_mdp1_1_qos,
	.num_links = 1,
	.links = { SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qnm_mnoc_hf_cfg = {
	.name = "qnm_mnoc_hf_cfg",
	.id = MASTER_CNOC_MNOC_HF_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { SLAVE_SERVICE_MNOC_HF },
};

static struct qcom_icc_node qnm_mnoc_sf_cfg = {
	.name = "qnm_mnoc_sf_cfg",
	.id = MASTER_CNOC_MNOC_SF_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { SLAVE_SERVICE_MNOC_SF },
};

static struct qcom_icc_qosbox qnm_video0_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x2a100 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 0,
		.prio = 0,
		.urg_fwd = 1,
	},
};

static struct qcom_icc_node qnm_video0 = {
	.name = "qnm_video0",
	.id = MASTER_VIDEO_P0,
	.channels = 1,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qnm_video0_qos,
	.num_links = 1,
	.links = { SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_qosbox qnm_video1_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x2a180 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 0,
		.prio = 0,
		.urg_fwd = 1,
	},
};

static struct qcom_icc_node qnm_video1 = {
	.name = "qnm_video1",
	.id = MASTER_VIDEO_P1,
	.channels = 1,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qnm_video1_qos,
	.num_links = 1,
	.links = { SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_qosbox qnm_video_cvp_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x2a200 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 0,
		.prio = 0,
		.urg_fwd = 1,
	},
};

static struct qcom_icc_node qnm_video_cvp = {
	.name = "qnm_video_cvp",
	.id = MASTER_VIDEO_PROC,
	.channels = 1,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qnm_video_cvp_qos,
	.num_links = 1,
	.links = { SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_qosbox qnm_video_v_cpu_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x2a280 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 0,
		.prio = 0,
		.urg_fwd = 1,
	},
};

static struct qcom_icc_node qnm_video_v_cpu = {
	.name = "qnm_video_v_cpu",
	.id = MASTER_VIDEO_V_PROC,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qnm_video_v_cpu_qos,
	.num_links = 1,
	.links = { SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qhm_nsp_noc_config = {
	.name = "qhm_nsp_noc_config",
	.id = MASTER_CDSP_NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { SLAVE_SERVICE_NSP_NOC },
};

static struct qcom_icc_node qxm_nsp = {
	.name = "qxm_nsp",
	.id = MASTER_CDSP_PROC,
	.channels = 2,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 2,
	.links = { SLAVE_HCP_A, SLAVE_CDSP_MEM_NOC },
};

static struct qcom_icc_node qhm_nspb_noc_config = {
	.name = "qhm_nspb_noc_config",
	.id = MASTER_CDSPB_NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { SLAVE_SERVICE_NSPB_NOC },
};

static struct qcom_icc_node qxm_nspb = {
	.name = "qxm_nspb",
	.id = MASTER_CDSP_PROC_B,
	.channels = 2,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 2,
	.links = { SLAVE_HCP_B, SLAVE_CDSPB_MEM_NOC },
};

static struct qcom_icc_qosbox xm_pcie3_0_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0xb000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node xm_pcie3_0 = {
	.name = "xm_pcie3_0",
	.id = MASTER_PCIE_0,
	.channels = 1,
	.buswidth = 16,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &xm_pcie3_0_qos,
	.num_links = 1,
	.links = { SLAVE_ANOC_PCIE_GEM_NOC },
};

static struct qcom_icc_qosbox xm_pcie3_1_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0xc000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node xm_pcie3_1 = {
	.name = "xm_pcie3_1",
	.id = MASTER_PCIE_1,
	.channels = 1,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &xm_pcie3_1_qos,
	.num_links = 1,
	.links = { SLAVE_ANOC_PCIE_GEM_NOC },
};

static struct qcom_icc_qosbox qhm_gic_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x14000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node qhm_gic = {
	.name = "qhm_gic",
	.id = MASTER_GIC_AHB,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qhm_gic_qos,
	.num_links = 1,
	.links = { SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node qnm_aggre1_noc = {
	.name = "qnm_aggre1_noc",
	.id = MASTER_A1NOC_SNOC,
	.channels = 1,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node qnm_aggre2_noc = {
	.name = "qnm_aggre2_noc",
	.id = MASTER_A2NOC_SNOC,
	.channels = 1,
	.buswidth = 16,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_qosbox qnm_lpass_noc_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x12000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 0,
		.prio = 0,
		.urg_fwd = 1,
	},
};

static struct qcom_icc_node qnm_lpass_noc = {
	.name = "qnm_lpass_noc",
	.id = MASTER_LPASS_ANOC,
	.channels = 1,
	.buswidth = 16,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qnm_lpass_noc_qos,
	.num_links = 1,
	.links = { SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node qnm_snoc_cfg = {
	.name = "qnm_snoc_cfg",
	.id = MASTER_SNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { SLAVE_SERVICE_SNOC },
};

static struct qcom_icc_qosbox qxm_pimem_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x13000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node qxm_pimem = {
	.name = "qxm_pimem",
	.id = MASTER_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &qxm_pimem_qos,
	.num_links = 1,
	.links = { SLAVE_SNOC_GEM_NOC_GC },
};

static struct qcom_icc_qosbox xm_gic_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x15000 },
	.config = &(struct qos_config) {
		.prio_fwd_disable = 1,
		.prio = 2,
		.urg_fwd = 0,
	},
};

static struct qcom_icc_node xm_gic = {
	.name = "xm_gic",
	.id = MASTER_GIC,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &xm_gic_qos,
	.num_links = 1,
	.links = { SLAVE_SNOC_GEM_NOC_GC },
};

static struct qcom_icc_node qns_a1noc_snoc = {
	.name = "qns_a1noc_snoc",
	.id = SLAVE_A1NOC_SNOC,
	.channels = 1,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_A1NOC_SNOC },
};

static struct qcom_icc_node qns_a2noc_snoc = {
	.name = "qns_a2noc_snoc",
	.id = SLAVE_A2NOC_SNOC,
	.channels = 1,
	.buswidth = 16,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_A2NOC_SNOC },
};

static struct qcom_icc_node qup0_core_slave = {
	.name = "qup0_core_slave",
	.id = SLAVE_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qup1_core_slave = {
	.name = "qup1_core_slave",
	.id = SLAVE_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qup2_core_slave = {
	.name = "qup2_core_slave",
	.id = SLAVE_QUP_CORE_2,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qup3_core_slave = {
	.name = "qup3_core_slave",
	.id = SLAVE_QUP_CORE_3,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ahb2phy0 = {
	.name = "qhs_ahb2phy0",
	.id = SLAVE_AHB2PHY_0,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ahb2phy1 = {
	.name = "qhs_ahb2phy1",
	.id = SLAVE_AHB2PHY_1,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ahb2phy2 = {
	.name = "qhs_ahb2phy2",
	.id = SLAVE_AHB2PHY_2,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ahb2phy3 = {
	.name = "qhs_ahb2phy3",
	.id = SLAVE_AHB2PHY_3,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_anoc_throttle_cfg = {
	.name = "qhs_anoc_throttle_cfg",
	.id = SLAVE_ANOC_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_aoss = {
	.name = "qhs_aoss",
	.id = SLAVE_AOSS,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_apss = {
	.name = "qhs_apss",
	.id = SLAVE_APPSS,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_boot_rom = {
	.name = "qhs_boot_rom",
	.id = SLAVE_BOOT_ROM,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_camera_cfg = {
	.name = "qhs_camera_cfg",
	.id = SLAVE_CAMERA_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_camera_nrt_throttle_cfg = {
	.name = "qhs_camera_nrt_throttle_cfg",
	.id = SLAVE_CAMERA_NRT_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_camera_rt_throttle_cfg = {
	.name = "qhs_camera_rt_throttle_cfg",
	.id = SLAVE_CAMERA_RT_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_compute0_cfg = {
	.name = "qhs_compute0_cfg",
	.id = SLAVE_CDSP_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_CDSP_NOC_CFG },
};

static struct qcom_icc_node qhs_compute1_cfg = {
	.name = "qhs_compute1_cfg",
	.id = SLAVE_CDSP1_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_CDSPB_NOC_CFG },
};

static struct qcom_icc_node qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = SLAVE_RBCPR_CX_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_cpr_mmcx = {
	.name = "qhs_cpr_mmcx",
	.id = SLAVE_RBCPR_MMCX_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_cpr_mx = {
	.name = "qhs_cpr_mx",
	.id = SLAVE_RBCPR_MX_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_cpr_nspcx = {
	.name = "qhs_cpr_nspcx",
	.id = SLAVE_CPR_NSPCX,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_cx_rdpm = {
	.name = "qhs_cx_rdpm",
	.id = SLAVE_CX_RDPM,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_display0_cfg = {
	.name = "qhs_display0_cfg",
	.id = SLAVE_DISPLAY_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_display0_rt_throttle_cfg = {
	.name = "qhs_display0_rt_throttle_cfg",
	.id = SLAVE_DISPLAY_RT_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_display1_cfg = {
	.name = "qhs_display1_cfg",
	.id = SLAVE_DISPLAY1_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_display1_rt_throttle_cfg = {
	.name = "qhs_display1_rt_throttle_cfg",
	.id = SLAVE_DISPLAY1_RT_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_emac0_cfg = {
	.name = "qhs_emac0_cfg",
	.id = SLAVE_EMAC_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_emac1_cfg = {
	.name = "qhs_emac1_cfg",
	.id = SLAVE_EMAC1_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_gp_dsp0_cfg = {
	.name = "qhs_gp_dsp0_cfg",
	.id = SLAVE_GP_DSP0_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_gp_dsp1_cfg = {
	.name = "qhs_gp_dsp1_cfg",
	.id = SLAVE_GP_DSP1_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_gpdsp0_throttle_cfg = {
	.name = "qhs_gpdsp0_throttle_cfg",
	.id = SLAVE_GPDSP0_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_gpdsp1_throttle_cfg = {
	.name = "qhs_gpdsp1_throttle_cfg",
	.id = SLAVE_GPDSP1_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_gpu_tcu_throttle_cfg = {
	.name = "qhs_gpu_tcu_throttle_cfg",
	.id = SLAVE_GPU_TCU_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.id = SLAVE_GFX3D_CFG,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_hwkm = {
	.name = "qhs_hwkm",
	.id = SLAVE_HWKM,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ipa = {
	.name = "qhs_ipa",
	.id = SLAVE_IPA_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ipc_router = {
	.name = "qhs_ipc_router",
	.id = SLAVE_IPC_ROUTER_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_lpass_cfg = {
	.name = "qhs_lpass_cfg",
	.id = SLAVE_LPASS,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_CNOC_LPASS_AG_NOC },
};

static struct qcom_icc_node qhs_lpass_throttle_cfg = {
	.name = "qhs_lpass_throttle_cfg",
	.id = SLAVE_LPASS_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_mx_rdpm = {
	.name = "qhs_mx_rdpm",
	.id = SLAVE_MX_RDPM,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_mxc_rdpm = {
	.name = "qhs_mxc_rdpm",
	.id = SLAVE_MXC_RDPM,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pcie0_cfg = {
	.name = "qhs_pcie0_cfg",
	.id = SLAVE_PCIE_0_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pcie1_cfg = {
	.name = "qhs_pcie1_cfg",
	.id = SLAVE_PCIE_1_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pcie_rsc_cfg = {
	.name = "qhs_pcie_rsc_cfg",
	.id = SLAVE_PCIE_RSC_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pcie_tcu_throttle_cfg = {
	.name = "qhs_pcie_tcu_throttle_cfg",
	.id = SLAVE_PCIE_TCU_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pcie_throttle_cfg = {
	.name = "qhs_pcie_throttle_cfg",
	.id = SLAVE_PCIE_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pdm = {
	.name = "qhs_pdm",
	.id = SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = SLAVE_PIMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pke_wrapper_cfg = {
	.name = "qhs_pke_wrapper_cfg",
	.id = SLAVE_PKA_WRAPPER_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qm_cfg = {
	.name = "qhs_qm_cfg",
	.id = SLAVE_QM_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qm_mpu_cfg = {
	.name = "qhs_qm_mpu_cfg",
	.id = SLAVE_QM_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qup0 = {
	.name = "qhs_qup0",
	.id = SLAVE_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qup1 = {
	.name = "qhs_qup1",
	.id = SLAVE_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qup2 = {
	.name = "qhs_qup2",
	.id = SLAVE_QUP_2,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qup3 = {
	.name = "qhs_qup3",
	.id = SLAVE_QUP_3,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_sail_throttle_cfg = {
	.name = "qhs_sail_throttle_cfg",
	.id = SLAVE_SAIL_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_sdc1 = {
	.name = "qhs_sdc1",
	.id = SLAVE_SDC1,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_security = {
	.name = "qhs_security",
	.id = SLAVE_SECURITY,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_snoc_throttle_cfg = {
	.name = "qhs_snoc_throttle_cfg",
	.id = SLAVE_SNOC_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_tlmm = {
	.name = "qhs_tlmm",
	.id = SLAVE_TLMM,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_tsc_cfg = {
	.name = "qhs_tsc_cfg",
	.id = SLAVE_TSC_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ufs_card_cfg = {
	.name = "qhs_ufs_card_cfg",
	.id = SLAVE_UFS_CARD_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.id = SLAVE_UFS_MEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_usb2_0 = {
	.name = "qhs_usb2_0",
	.id = SLAVE_USB2,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_usb3_0 = {
	.name = "qhs_usb3_0",
	.id = SLAVE_USB3_0,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_usb3_1 = {
	.name = "qhs_usb3_1",
	.id = SLAVE_USB3_1,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = SLAVE_VENUS_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_venus_cvp_throttle_cfg = {
	.name = "qhs_venus_cvp_throttle_cfg",
	.id = SLAVE_VENUS_CVP_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_venus_v_cpu_throttle_cfg = {
	.name = "qhs_venus_v_cpu_throttle_cfg",
	.id = SLAVE_VENUS_V_CPU_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_venus_vcodec_throttle_cfg = {
	.name = "qhs_venus_vcodec_throttle_cfg",
	.id = SLAVE_VENUS_VCODEC_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qns_ddrss_cfg = {
	.name = "qns_ddrss_cfg",
	.id = SLAVE_DDRSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_CNOC_DC_NOC },
};

static struct qcom_icc_node qns_gpdsp_noc_cfg = {
	.name = "qns_gpdsp_noc_cfg",
	.id = SLAVE_GPDSP_NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qns_mnoc_hf_cfg = {
	.name = "qns_mnoc_hf_cfg",
	.id = SLAVE_CNOC_MNOC_HF_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_CNOC_MNOC_HF_CFG },
};

static struct qcom_icc_node qns_mnoc_sf_cfg = {
	.name = "qns_mnoc_sf_cfg",
	.id = SLAVE_CNOC_MNOC_SF_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_CNOC_MNOC_SF_CFG },
};

static struct qcom_icc_node qns_pcie_anoc_cfg = {
	.name = "qns_pcie_anoc_cfg",
	.id = SLAVE_PCIE_ANOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qns_snoc_cfg = {
	.name = "qns_snoc_cfg",
	.id = SLAVE_SNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_SNOC_CFG },
};

static struct qcom_icc_node qxs_boot_imem = {
	.name = "qxs_boot_imem",
	.id = SLAVE_BOOT_IMEM,
	.channels = 1,
	.buswidth = 16,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qxs_imem = {
	.name = "qxs_imem",
	.id = SLAVE_IMEM,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qxs_pimem = {
	.name = "qxs_pimem",
	.id = SLAVE_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node xs_pcie_0 = {
	.name = "xs_pcie_0",
	.id = SLAVE_PCIE_0,
	.channels = 1,
	.buswidth = 16,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node xs_pcie_1 = {
	.name = "xs_pcie_1",
	.id = SLAVE_PCIE_1,
	.channels = 1,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SLAVE_TCU,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_llcc = {
	.name = "qhs_llcc",
	.id = SLAVE_LLCC_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qns_gemnoc = {
	.name = "qns_gemnoc",
	.id = SLAVE_GEM_NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_GEM_NOC_CFG },
};

static struct qcom_icc_node qns_gem_noc_cnoc = {
	.name = "qns_gem_noc_cnoc",
	.id = SLAVE_GEM_NOC_CNOC,
	.channels = 1,
	.buswidth = 16,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_GEM_NOC_CNOC },
};

static struct qcom_icc_node qns_llcc = {
	.name = "qns_llcc",
	.id = SLAVE_LLCC,
	.channels = 6,
	.buswidth = 16,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_LLCC },
};

static struct qcom_icc_node qns_pcie = {
	.name = "qns_pcie",
	.id = SLAVE_GEM_NOC_PCIE_CNOC,
	.channels = 1,
	.buswidth = 16,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_GEM_NOC_PCIE_SNOC },
};

static struct qcom_icc_node srvc_even_gemnoc = {
	.name = "srvc_even_gemnoc",
	.id = SLAVE_SERVICE_GEM_NOC_1,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node srvc_odd_gemnoc = {
	.name = "srvc_odd_gemnoc",
	.id = SLAVE_SERVICE_GEM_NOC_2,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node srvc_sys_gemnoc = {
	.name = "srvc_sys_gemnoc",
	.id = SLAVE_SERVICE_GEM_NOC,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node srvc_sys_gemnoc_2 = {
	.name = "srvc_sys_gemnoc_2",
	.id = SLAVE_SERVICE_GEM_NOC2,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qns_gp_dsp_sail_noc = {
	.name = "qns_gp_dsp_sail_noc",
	.id = SLAVE_GP_DSP_SAIL_NOC,
	.channels = 1,
	.buswidth = 16,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_GPDSP_SAIL },
};

static struct qcom_icc_node qhs_lpass_core = {
	.name = "qhs_lpass_core",
	.id = SLAVE_LPASS_CORE_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_lpass_lpi = {
	.name = "qhs_lpass_lpi",
	.id = SLAVE_LPASS_LPI_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_lpass_mpu = {
	.name = "qhs_lpass_mpu",
	.id = SLAVE_LPASS_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qhs_lpass_top = {
	.name = "qhs_lpass_top",
	.id = SLAVE_LPASS_TOP_CFG,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qns_sysnoc = {
	.name = "qns_sysnoc",
	.id = SLAVE_LPASS_SNOC,
	.channels = 1,
	.buswidth = 16,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_LPASS_ANOC },
};

static struct qcom_icc_node srvc_niu_aml_noc = {
	.name = "srvc_niu_aml_noc",
	.id = SLAVE_SERVICES_LPASS_AML_NOC,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node srvc_niu_lpass_agnoc = {
	.name = "srvc_niu_lpass_agnoc",
	.id = SLAVE_SERVICE_LPASS_AG_NOC,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.id = SLAVE_EBI1,
	.channels = 8,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qns_mem_noc_hf = {
	.name = "qns_mem_noc_hf",
	.id = SLAVE_MNOC_HF_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qns_mem_noc_sf = {
	.name = "qns_mem_noc_sf",
	.id = SLAVE_MNOC_SF_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node srvc_mnoc_hf = {
	.name = "srvc_mnoc_hf",
	.id = SLAVE_SERVICE_MNOC_HF,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node srvc_mnoc_sf = {
	.name = "srvc_mnoc_sf",
	.id = SLAVE_SERVICE_MNOC_SF,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qns_hcp = {
	.name = "qns_hcp",
	.id = SLAVE_HCP_A,
	.channels = 2,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qns_nsp_gemnoc = {
	.name = "qns_nsp_gemnoc",
	.id = SLAVE_CDSP_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_COMPUTE_NOC },
};

static struct qcom_icc_node service_nsp_noc = {
	.name = "service_nsp_noc",
	.id = SLAVE_SERVICE_NSP_NOC,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qns_nspb_gemnoc = {
	.name = "qns_nspb_gemnoc",
	.id = SLAVE_CDSPB_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_COMPUTE_NOC_1 },
};

static struct qcom_icc_node qns_nspb_hcp = {
	.name = "qns_nspb_hcp",
	.id = SLAVE_HCP_B,
	.channels = 2,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node service_nspb_noc = {
	.name = "service_nspb_noc",
	.id = SLAVE_SERVICE_NSPB_NOC,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_node qns_pcie_mem_noc = {
	.name = "qns_pcie_mem_noc",
	.id = SLAVE_ANOC_PCIE_GEM_NOC,
	.channels = 1,
	.buswidth = 32,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_ANOC_PCIE_GEM_NOC },
};

static struct qcom_icc_node qns_gemnoc_gc = {
	.name = "qns_gemnoc_gc",
	.id = SLAVE_SNOC_GEM_NOC_GC,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_SNOC_GC_MEM_NOC },
};

static struct qcom_icc_node qns_gemnoc_sf = {
	.name = "qns_gemnoc_sf",
	.id = SLAVE_SNOC_GEM_NOC_SF,
	.channels = 1,
	.buswidth = 16,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 1,
	.links = { MASTER_SNOC_SF_MEM_NOC },
};

static struct qcom_icc_node srvc_snoc = {
	.name = "srvc_snoc",
	.id = SLAVE_SERVICE_SNOC,
	.channels = 1,
	.buswidth = 4,
	.noc_ops = &qcom_qnoc4_ops,
	.num_links = 0,
};

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
	.voter_idx = VOTER_IDX_HLOS,
	.enable_mask = 0x8,
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.voter_idx = VOTER_IDX_HLOS,
	.num_nodes = 2,
	.nodes = { &qxm_crypto_0, &qxm_crypto_1 },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.voter_idx = VOTER_IDX_HLOS,
	.keepalive = true,
	.num_nodes = 2,
	.nodes = { &qnm_gemnoc_cnoc, &qnm_gemnoc_pcie },
};

static struct qcom_icc_bcm bcm_cn1 = {
	.name = "CN1",
	.voter_idx = VOTER_IDX_HLOS,
	.num_nodes = 76,
	.nodes = { &qhs_ahb2phy0, &qhs_ahb2phy1,
		   &qhs_ahb2phy2, &qhs_ahb2phy3,
		   &qhs_anoc_throttle_cfg, &qhs_aoss,
		   &qhs_apss, &qhs_boot_rom,
		   &qhs_camera_cfg, &qhs_camera_nrt_throttle_cfg,
		   &qhs_camera_rt_throttle_cfg, &qhs_clk_ctl,
		   &qhs_compute0_cfg, &qhs_compute1_cfg,
		   &qhs_cpr_cx, &qhs_cpr_mmcx,
		   &qhs_cpr_mx, &qhs_cpr_nspcx,
		   &qhs_crypto0_cfg, &qhs_cx_rdpm,
		   &qhs_display0_cfg, &qhs_display0_rt_throttle_cfg,
		   &qhs_display1_cfg, &qhs_display1_rt_throttle_cfg,
		   &qhs_emac0_cfg, &qhs_emac1_cfg,
		   &qhs_gp_dsp0_cfg, &qhs_gp_dsp1_cfg,
		   &qhs_gpdsp0_throttle_cfg, &qhs_gpdsp1_throttle_cfg,
		   &qhs_gpu_tcu_throttle_cfg, &qhs_gpuss_cfg,
		   &qhs_hwkm, &qhs_imem_cfg,
		   &qhs_ipa, &qhs_ipc_router,
		   &qhs_lpass_cfg, &qhs_lpass_throttle_cfg,
		   &qhs_mx_rdpm, &qhs_mxc_rdpm,
		   &qhs_pcie0_cfg, &qhs_pcie1_cfg,
		   &qhs_pcie_rsc_cfg, &qhs_pcie_tcu_throttle_cfg,
		   &qhs_pcie_throttle_cfg, &qhs_pdm,
		   &qhs_pimem_cfg, &qhs_pke_wrapper_cfg,
		   &qhs_qdss_cfg, &qhs_qm_cfg,
		   &qhs_qm_mpu_cfg, &qhs_sail_throttle_cfg,
		   &qhs_sdc1, &qhs_security,
		   &qhs_snoc_throttle_cfg, &qhs_tcsr,
		   &qhs_tlmm, &qhs_tsc_cfg,
		   &qhs_ufs_card_cfg, &qhs_ufs_mem_cfg,
		   &qhs_usb2_0, &qhs_usb3_0,
		   &qhs_usb3_1, &qhs_venus_cfg,
		   &qhs_venus_cvp_throttle_cfg, &qhs_venus_v_cpu_throttle_cfg,
		   &qhs_venus_vcodec_throttle_cfg, &qns_ddrss_cfg,
		   &qns_gpdsp_noc_cfg, &qns_mnoc_hf_cfg,
		   &qns_mnoc_sf_cfg, &qns_pcie_anoc_cfg,
		   &qns_snoc_cfg, &qxs_boot_imem,
		   &qxs_imem, &xs_sys_tcu_cfg },
};

static struct qcom_icc_bcm bcm_cn2 = {
	.name = "CN2",
	.voter_idx = VOTER_IDX_HLOS,
	.num_nodes = 4,
	.nodes = { &qhs_qup0, &qhs_qup1,
		   &qhs_qup2, &qhs_qup3 },
};

static struct qcom_icc_bcm bcm_cn3 = {
	.name = "CN3",
	.voter_idx = VOTER_IDX_HLOS,
	.num_nodes = 2,
	.nodes = { &xs_pcie_0, &xs_pcie_1 },
};

static struct qcom_icc_bcm bcm_gna0 = {
	.name = "GNA0",
	.voter_idx = VOTER_IDX_HLOS,
	.num_nodes = 1,
	.nodes = { &qxm_dsp0 },
};

static struct qcom_icc_bcm bcm_gnb0 = {
	.name = "GNB0",
	.voter_idx = VOTER_IDX_HLOS,
	.num_nodes = 1,
	.nodes = { &qxm_dsp1 },
};

static struct qcom_icc_bcm bcm_mc0 = {
	.name = "MC0",
	.voter_idx = VOTER_IDX_HLOS,
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_mm0 = {
	.name = "MM0",
	.voter_idx = VOTER_IDX_HLOS,
	.keepalive = true,
	.num_nodes = 5,
	.nodes = { &qnm_camnoc_hf, &qnm_mdp0_0,
		   &qnm_mdp0_1, &qnm_mdp1_0,
		   &qns_mem_noc_hf },
};

static struct qcom_icc_bcm bcm_mm1 = {
	.name = "MM1",
	.voter_idx = VOTER_IDX_HLOS,
	.num_nodes = 7,
	.nodes = { &qnm_camnoc_icp, &qnm_camnoc_sf,
		   &qnm_video0, &qnm_video1,
		   &qnm_video_cvp, &qnm_video_v_cpu,
		   &qns_mem_noc_sf },
};

static struct qcom_icc_bcm bcm_nsa0 = {
	.name = "NSA0",
	.voter_idx = VOTER_IDX_HLOS,
	.num_nodes = 2,
	.nodes = { &qns_hcp, &qns_nsp_gemnoc },
};

static struct qcom_icc_bcm bcm_nsa1 = {
	.name = "NSA1",
	.voter_idx = VOTER_IDX_HLOS,
	.num_nodes = 1,
	.nodes = { &qxm_nsp },
};

static struct qcom_icc_bcm bcm_nsb0 = {
	.name = "NSB0",
	.voter_idx = VOTER_IDX_HLOS,
	.num_nodes = 2,
	.nodes = { &qns_nspb_gemnoc, &qns_nspb_hcp },
};

static struct qcom_icc_bcm bcm_nsb1 = {
	.name = "NSB1",
	.voter_idx = VOTER_IDX_HLOS,
	.num_nodes = 1,
	.nodes = { &qxm_nspb },
};

static struct qcom_icc_bcm bcm_pci0 = {
	.name = "PCI0",
	.voter_idx = VOTER_IDX_HLOS,
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_pcie_mem_noc },
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.voter_idx = VOTER_IDX_HLOS,
	.vote_scale = 1,
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qup0_core_slave },
};

static struct qcom_icc_bcm bcm_qup1 = {
	.name = "QUP1",
	.voter_idx = VOTER_IDX_HLOS,
	.vote_scale = 1,
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qup1_core_slave },
};

static struct qcom_icc_bcm bcm_qup2 = {
	.name = "QUP2",
	.voter_idx = VOTER_IDX_HLOS,
	.vote_scale = 1,
	.keepalive = true,
	.num_nodes = 2,
	.nodes = { &qup2_core_slave, &qup3_core_slave },
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.voter_idx = VOTER_IDX_HLOS,
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_sh2 = {
	.name = "SH2",
	.voter_idx = VOTER_IDX_HLOS,
	.num_nodes = 1,
	.nodes = { &chm_apps },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.voter_idx = VOTER_IDX_HLOS,
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.voter_idx = VOTER_IDX_HLOS,
	.num_nodes = 1,
	.nodes = { &qns_gemnoc_gc },
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.voter_idx = VOTER_IDX_HLOS,
	.num_nodes = 1,
	.nodes = { &qxs_pimem },
};

static struct qcom_icc_bcm bcm_sn3 = {
	.name = "SN3",
	.voter_idx = VOTER_IDX_HLOS,
	.num_nodes = 2,
	.nodes = { &qns_a1noc_snoc, &qnm_aggre1_noc },
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.voter_idx = VOTER_IDX_HLOS,
	.num_nodes = 2,
	.nodes = { &qns_a2noc_snoc, &qnm_aggre2_noc },
};

static struct qcom_icc_bcm bcm_sn9 = {
	.name = "SN9",
	.voter_idx = VOTER_IDX_HLOS,
	.num_nodes = 2,
	.nodes = { &qns_sysnoc, &qnm_lpass_noc },
};

static struct qcom_icc_bcm bcm_sn10 = {
	.name = "SN10",
	.voter_idx = VOTER_IDX_HLOS,
	.num_nodes = 1,
	.nodes = { &xs_qdss_stm },
};

static struct qcom_icc_bcm *aggre1_noc_bcms[] = {
	&bcm_sn3,
};

static struct qcom_icc_node *aggre1_noc_nodes[] = {
	[MASTER_QUP_3] = &qxm_qup3,
	[MASTER_EMAC] = &xm_emac_0,
	[MASTER_EMAC_1] = &xm_emac_1,
	[MASTER_SDC] = &xm_sdc1,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_USB2] = &xm_usb2_2,
	[MASTER_USB3_0] = &xm_usb3_0,
	[MASTER_USB3_1] = &xm_usb3_1,
	[SLAVE_A1NOC_SNOC] = &qns_a1noc_snoc,
};

static char *aggre1_noc_voters[] = {
	[VOTER_IDX_HLOS] = "hlos",
};

static struct qcom_icc_desc lemans_aggre1_noc = {
	.config = &icc_regmap_config,
	.nodes = aggre1_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre1_noc_nodes),
	.bcms = aggre1_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_noc_bcms),
	.voters = aggre1_noc_voters,
	.num_voters = ARRAY_SIZE(aggre1_noc_voters),
};

static struct qcom_icc_bcm *aggre2_noc_bcms[] = {
	&bcm_ce0,
	&bcm_sn4,
};

static struct qcom_icc_node *aggre2_noc_nodes[] = {
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_QUP_1] = &qhm_qup1,
	[MASTER_QUP_2] = &qhm_qup2,
	[MASTER_CNOC_A2NOC] = &qnm_cnoc_datapath,
	[MASTER_CRYPTO_CORE0] = &qxm_crypto_0,
	[MASTER_CRYPTO_CORE1] = &qxm_crypto_1,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_QDSS_ETR_0] = &xm_qdss_etr_0,
	[MASTER_QDSS_ETR_1] = &xm_qdss_etr_1,
	[MASTER_UFS_CARD] = &xm_ufs_card,
	[SLAVE_A2NOC_SNOC] = &qns_a2noc_snoc,
};

static char *aggre2_noc_voters[] = {
	[VOTER_IDX_HLOS] = "hlos",
};

static struct qcom_icc_desc lemans_aggre2_noc = {
	.config = &icc_regmap_config,
	.nodes = aggre2_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre2_noc_nodes),
	.bcms = aggre2_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_noc_bcms),
	.voters = aggre2_noc_voters,
	.num_voters = ARRAY_SIZE(aggre2_noc_voters),
};

static struct qcom_icc_bcm *clk_virt_bcms[] = {
	&bcm_qup0,
	&bcm_qup1,
	&bcm_qup2,
};

static struct qcom_icc_node *clk_virt_nodes[] = {
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[MASTER_QUP_CORE_1] = &qup1_core_master,
	[MASTER_QUP_CORE_2] = &qup2_core_master,
	[MASTER_QUP_CORE_3] = &qup3_core_master,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[SLAVE_QUP_CORE_1] = &qup1_core_slave,
	[SLAVE_QUP_CORE_2] = &qup2_core_slave,
	[SLAVE_QUP_CORE_3] = &qup3_core_slave,
};

static char *clk_virt_voters[] = {
	[VOTER_IDX_HLOS] = "hlos",
};

static struct qcom_icc_desc lemans_clk_virt = {
	.config = &icc_regmap_config,
	.nodes = clk_virt_nodes,
	.num_nodes = ARRAY_SIZE(clk_virt_nodes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
	.voters = clk_virt_voters,
	.num_voters = ARRAY_SIZE(clk_virt_voters),
};

static struct qcom_icc_bcm *config_noc_bcms[] = {
	&bcm_cn0,
	&bcm_cn1,
	&bcm_cn2,
	&bcm_cn3,
	&bcm_sn2,
	&bcm_sn10,
};

static struct qcom_icc_node *config_noc_nodes[] = {
	[MASTER_GEM_NOC_CNOC] = &qnm_gemnoc_cnoc,
	[MASTER_GEM_NOC_PCIE_SNOC] = &qnm_gemnoc_pcie,
	[SLAVE_AHB2PHY_0] = &qhs_ahb2phy0,
	[SLAVE_AHB2PHY_1] = &qhs_ahb2phy1,
	[SLAVE_AHB2PHY_2] = &qhs_ahb2phy2,
	[SLAVE_AHB2PHY_3] = &qhs_ahb2phy3,
	[SLAVE_ANOC_THROTTLE_CFG] = &qhs_anoc_throttle_cfg,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_APPSS] = &qhs_apss,
	[SLAVE_BOOT_ROM] = &qhs_boot_rom,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CAMERA_NRT_THROTTLE_CFG] = &qhs_camera_nrt_throttle_cfg,
	[SLAVE_CAMERA_RT_THROTTLE_CFG] = &qhs_camera_rt_throttle_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CDSP_CFG] = &qhs_compute0_cfg,
	[SLAVE_CDSP1_CFG] = &qhs_compute1_cfg,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MMCX_CFG] = &qhs_cpr_mmcx,
	[SLAVE_RBCPR_MX_CFG] = &qhs_cpr_mx,
	[SLAVE_CPR_NSPCX] = &qhs_cpr_nspcx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_CX_RDPM] = &qhs_cx_rdpm,
	[SLAVE_DISPLAY_CFG] = &qhs_display0_cfg,
	[SLAVE_DISPLAY_RT_THROTTLE_CFG] = &qhs_display0_rt_throttle_cfg,
	[SLAVE_DISPLAY1_CFG] = &qhs_display1_cfg,
	[SLAVE_DISPLAY1_RT_THROTTLE_CFG] = &qhs_display1_rt_throttle_cfg,
	[SLAVE_EMAC_CFG] = &qhs_emac0_cfg,
	[SLAVE_EMAC1_CFG] = &qhs_emac1_cfg,
	[SLAVE_GP_DSP0_CFG] = &qhs_gp_dsp0_cfg,
	[SLAVE_GP_DSP1_CFG] = &qhs_gp_dsp1_cfg,
	[SLAVE_GPDSP0_THROTTLE_CFG] = &qhs_gpdsp0_throttle_cfg,
	[SLAVE_GPDSP1_THROTTLE_CFG] = &qhs_gpdsp1_throttle_cfg,
	[SLAVE_GPU_TCU_THROTTLE_CFG] = &qhs_gpu_tcu_throttle_cfg,
	[SLAVE_GFX3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_HWKM] = &qhs_hwkm,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_IPC_ROUTER_CFG] = &qhs_ipc_router,
	[SLAVE_LPASS] = &qhs_lpass_cfg,
	[SLAVE_LPASS_THROTTLE_CFG] = &qhs_lpass_throttle_cfg,
	[SLAVE_MX_RDPM] = &qhs_mx_rdpm,
	[SLAVE_MXC_RDPM] = &qhs_mxc_rdpm,
	[SLAVE_PCIE_0_CFG] = &qhs_pcie0_cfg,
	[SLAVE_PCIE_1_CFG] = &qhs_pcie1_cfg,
	[SLAVE_PCIE_RSC_CFG] = &qhs_pcie_rsc_cfg,
	[SLAVE_PCIE_TCU_THROTTLE_CFG] = &qhs_pcie_tcu_throttle_cfg,
	[SLAVE_PCIE_THROTTLE_CFG] = &qhs_pcie_throttle_cfg,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PKA_WRAPPER_CFG] = &qhs_pke_wrapper_cfg,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QM_CFG] = &qhs_qm_cfg,
	[SLAVE_QM_MPU_CFG] = &qhs_qm_mpu_cfg,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_QUP_2] = &qhs_qup2,
	[SLAVE_QUP_3] = &qhs_qup3,
	[SLAVE_SAIL_THROTTLE_CFG] = &qhs_sail_throttle_cfg,
	[SLAVE_SDC1] = &qhs_sdc1,
	[SLAVE_SECURITY] = &qhs_security,
	[SLAVE_SNOC_THROTTLE_CFG] = &qhs_snoc_throttle_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_TSC_CFG] = &qhs_tsc_cfg,
	[SLAVE_UFS_CARD_CFG] = &qhs_ufs_card_cfg,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB2] = &qhs_usb2_0,
	[SLAVE_USB3_0] = &qhs_usb3_0,
	[SLAVE_USB3_1] = &qhs_usb3_1,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VENUS_CVP_THROTTLE_CFG] = &qhs_venus_cvp_throttle_cfg,
	[SLAVE_VENUS_V_CPU_THROTTLE_CFG] = &qhs_venus_v_cpu_throttle_cfg,
	[SLAVE_VENUS_VCODEC_THROTTLE_CFG] = &qhs_venus_vcodec_throttle_cfg,
	[SLAVE_DDRSS_CFG] = &qns_ddrss_cfg,
	[SLAVE_GPDSP_NOC_CFG] = &qns_gpdsp_noc_cfg,
	[SLAVE_CNOC_MNOC_HF_CFG] = &qns_mnoc_hf_cfg,
	[SLAVE_CNOC_MNOC_SF_CFG] = &qns_mnoc_sf_cfg,
	[SLAVE_PCIE_ANOC_CFG] = &qns_pcie_anoc_cfg,
	[SLAVE_SNOC_CFG] = &qns_snoc_cfg,
	[SLAVE_BOOT_IMEM] = &qxs_boot_imem,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_PCIE_0] = &xs_pcie_0,
	[SLAVE_PCIE_1] = &xs_pcie_1,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static char *config_noc_voters[] = {
	[VOTER_IDX_HLOS] = "hlos",
};

static struct qcom_icc_desc lemans_config_noc = {
	.config = &icc_regmap_config,
	.nodes = config_noc_nodes,
	.num_nodes = ARRAY_SIZE(config_noc_nodes),
	.bcms = config_noc_bcms,
	.num_bcms = ARRAY_SIZE(config_noc_bcms),
	.voters = config_noc_voters,
	.num_voters = ARRAY_SIZE(config_noc_voters),
};

static struct qcom_icc_bcm *dc_noc_bcms[] = {
};

static struct qcom_icc_node *dc_noc_nodes[] = {
	[MASTER_CNOC_DC_NOC] = &qnm_cnoc_dc_noc,
	[SLAVE_LLCC_CFG] = &qhs_llcc,
	[SLAVE_GEM_NOC_CFG] = &qns_gemnoc,
};

static char *dc_noc_voters[] = {
	[VOTER_IDX_HLOS] = "hlos",
};

static struct qcom_icc_desc lemans_dc_noc = {
	.config = &icc_regmap_config,
	.nodes = dc_noc_nodes,
	.num_nodes = ARRAY_SIZE(dc_noc_nodes),
	.bcms = dc_noc_bcms,
	.num_bcms = ARRAY_SIZE(dc_noc_bcms),
	.voters = dc_noc_voters,
	.num_voters = ARRAY_SIZE(dc_noc_voters),
};

static struct qcom_icc_bcm *gem_noc_bcms[] = {
	&bcm_sh0,
	&bcm_sh2,
};

static struct qcom_icc_node *gem_noc_nodes[] = {
	[MASTER_GPU_TCU] = &alm_gpu_tcu,
	[MASTER_PCIE_TCU] = &alm_pcie_tcu,
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_APPSS_PROC] = &chm_apps,
	[MASTER_COMPUTE_NOC] = &qnm_cmpnoc0,
	[MASTER_COMPUTE_NOC_1] = &qnm_cmpnoc1,
	[MASTER_GEM_NOC_CFG] = &qnm_gemnoc_cfg,
	[MASTER_GPDSP_SAIL] = &qnm_gpdsp_sail,
	[MASTER_GFX3D] = &qnm_gpu,
	[MASTER_MNOC_HF_MEM_NOC] = &qnm_mnoc_hf,
	[MASTER_MNOC_SF_MEM_NOC] = &qnm_mnoc_sf,
	[MASTER_ANOC_PCIE_GEM_NOC] = &qnm_pcie,
	[MASTER_SNOC_GC_MEM_NOC] = &qnm_snoc_gc,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[SLAVE_GEM_NOC_CNOC] = &qns_gem_noc_cnoc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_GEM_NOC_PCIE_CNOC] = &qns_pcie,
	[SLAVE_SERVICE_GEM_NOC_1] = &srvc_even_gemnoc,
	[SLAVE_SERVICE_GEM_NOC_2] = &srvc_odd_gemnoc,
	[SLAVE_SERVICE_GEM_NOC] = &srvc_sys_gemnoc,
	[SLAVE_SERVICE_GEM_NOC2] = &srvc_sys_gemnoc_2,
};

static char *gem_noc_voters[] = {
	[VOTER_IDX_HLOS] = "hlos",
};

static struct qcom_icc_desc lemans_gem_noc = {
	.config = &icc_regmap_config,
	.nodes = gem_noc_nodes,
	.num_nodes = ARRAY_SIZE(gem_noc_nodes),
	.bcms = gem_noc_bcms,
	.num_bcms = ARRAY_SIZE(gem_noc_bcms),
	.voters = gem_noc_voters,
	.num_voters = ARRAY_SIZE(gem_noc_voters),
};

static struct qcom_icc_bcm *gpdsp_anoc_bcms[] = {
	&bcm_gna0,
	&bcm_gnb0,
};

static struct qcom_icc_node *gpdsp_anoc_nodes[] = {
	[MASTER_DSP0] = &qxm_dsp0,
	[MASTER_DSP1] = &qxm_dsp1,
	[SLAVE_GP_DSP_SAIL_NOC] = &qns_gp_dsp_sail_noc,
};

static char *gpdsp_anoc_voters[] = {
	[VOTER_IDX_HLOS] = "hlos",
};

static struct qcom_icc_desc lemans_gpdsp_anoc = {
	.config = &icc_regmap_config,
	.nodes = gpdsp_anoc_nodes,
	.num_nodes = ARRAY_SIZE(gpdsp_anoc_nodes),
	.bcms = gpdsp_anoc_bcms,
	.num_bcms = ARRAY_SIZE(gpdsp_anoc_bcms),
	.voters = gpdsp_anoc_voters,
	.num_voters = ARRAY_SIZE(gpdsp_anoc_voters),
};

static struct qcom_icc_bcm *lpass_ag_noc_bcms[] = {
	&bcm_sn9,
};

static struct qcom_icc_node *lpass_ag_noc_nodes[] = {
	[MASTER_CNOC_LPASS_AG_NOC] = &qhm_config_noc,
	[MASTER_LPASS_PROC] = &qxm_lpass_dsp,
	[SLAVE_LPASS_CORE_CFG] = &qhs_lpass_core,
	[SLAVE_LPASS_LPI_CFG] = &qhs_lpass_lpi,
	[SLAVE_LPASS_MPU_CFG] = &qhs_lpass_mpu,
	[SLAVE_LPASS_TOP_CFG] = &qhs_lpass_top,
	[SLAVE_LPASS_SNOC] = &qns_sysnoc,
	[SLAVE_SERVICES_LPASS_AML_NOC] = &srvc_niu_aml_noc,
	[SLAVE_SERVICE_LPASS_AG_NOC] = &srvc_niu_lpass_agnoc,
};

static char *lpass_ag_noc_voters[] = {
	[VOTER_IDX_HLOS] = "hlos",
};

static struct qcom_icc_desc lemans_lpass_ag_noc = {
	.config = &icc_regmap_config,
	.nodes = lpass_ag_noc_nodes,
	.num_nodes = ARRAY_SIZE(lpass_ag_noc_nodes),
	.bcms = lpass_ag_noc_bcms,
	.num_bcms = ARRAY_SIZE(lpass_ag_noc_bcms),
	.voters = lpass_ag_noc_voters,
	.num_voters = ARRAY_SIZE(lpass_ag_noc_voters),
};

static struct qcom_icc_bcm *mc_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
};

static struct qcom_icc_node *mc_virt_nodes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
};

static char *mc_virt_voters[] = {
	[VOTER_IDX_HLOS] = "hlos",
};

static struct qcom_icc_desc lemans_mc_virt = {
	.config = &icc_regmap_config,
	.nodes = mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
	.voters = mc_virt_voters,
	.num_voters = ARRAY_SIZE(mc_virt_voters),
};

static struct qcom_icc_bcm *mmss_noc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
};

static struct qcom_icc_node *mmss_noc_nodes[] = {
	[MASTER_CAMNOC_HF] = &qnm_camnoc_hf,
	[MASTER_CAMNOC_ICP] = &qnm_camnoc_icp,
	[MASTER_CAMNOC_SF] = &qnm_camnoc_sf,
	[MASTER_MDP0] = &qnm_mdp0_0,
	[MASTER_MDP1] = &qnm_mdp0_1,
	[MASTER_MDP_CORE1_0] = &qnm_mdp1_0,
	[MASTER_MDP_CORE1_1] = &qnm_mdp1_1,
	[MASTER_CNOC_MNOC_HF_CFG] = &qnm_mnoc_hf_cfg,
	[MASTER_CNOC_MNOC_SF_CFG] = &qnm_mnoc_sf_cfg,
	[MASTER_VIDEO_P0] = &qnm_video0,
	[MASTER_VIDEO_P1] = &qnm_video1,
	[MASTER_VIDEO_PROC] = &qnm_video_cvp,
	[MASTER_VIDEO_V_PROC] = &qnm_video_v_cpu,
	[SLAVE_MNOC_HF_MEM_NOC] = &qns_mem_noc_hf,
	[SLAVE_MNOC_SF_MEM_NOC] = &qns_mem_noc_sf,
	[SLAVE_SERVICE_MNOC_HF] = &srvc_mnoc_hf,
	[SLAVE_SERVICE_MNOC_SF] = &srvc_mnoc_sf,
};

static char *mmss_noc_voters[] = {
	[VOTER_IDX_HLOS] = "hlos",
};

static struct qcom_icc_desc lemans_mmss_noc = {
	.config = &icc_regmap_config,
	.nodes = mmss_noc_nodes,
	.num_nodes = ARRAY_SIZE(mmss_noc_nodes),
	.bcms = mmss_noc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_noc_bcms),
	.voters = mmss_noc_voters,
	.num_voters = ARRAY_SIZE(mmss_noc_voters),
};

static struct qcom_icc_bcm *nspa_noc_bcms[] = {
	&bcm_nsa0,
	&bcm_nsa1,
};

static struct qcom_icc_node *nspa_noc_nodes[] = {
	[MASTER_CDSP_NOC_CFG] = &qhm_nsp_noc_config,
	[MASTER_CDSP_PROC] = &qxm_nsp,
	[SLAVE_HCP_A] = &qns_hcp,
	[SLAVE_CDSP_MEM_NOC] = &qns_nsp_gemnoc,
	[SLAVE_SERVICE_NSP_NOC] = &service_nsp_noc,
};

static char *nspa_noc_voters[] = {
	[VOTER_IDX_HLOS] = "hlos",
};

static struct qcom_icc_desc lemans_nspa_noc = {
	.config = &icc_regmap_config,
	.nodes = nspa_noc_nodes,
	.num_nodes = ARRAY_SIZE(nspa_noc_nodes),
	.bcms = nspa_noc_bcms,
	.num_bcms = ARRAY_SIZE(nspa_noc_bcms),
	.voters = nspa_noc_voters,
	.num_voters = ARRAY_SIZE(nspa_noc_voters),
};

static struct qcom_icc_bcm *nspb_noc_bcms[] = {
	&bcm_nsb0,
	&bcm_nsb1,
};

static struct qcom_icc_node *nspb_noc_nodes[] = {
	[MASTER_CDSPB_NOC_CFG] = &qhm_nspb_noc_config,
	[MASTER_CDSP_PROC_B] = &qxm_nspb,
	[SLAVE_CDSPB_MEM_NOC] = &qns_nspb_gemnoc,
	[SLAVE_HCP_B] = &qns_nspb_hcp,
	[SLAVE_SERVICE_NSPB_NOC] = &service_nspb_noc,
};

static char *nspb_noc_voters[] = {
	[VOTER_IDX_HLOS] = "hlos",
};

static struct qcom_icc_desc lemans_nspb_noc = {
	.config = &icc_regmap_config,
	.nodes = nspb_noc_nodes,
	.num_nodes = ARRAY_SIZE(nspb_noc_nodes),
	.bcms = nspb_noc_bcms,
	.num_bcms = ARRAY_SIZE(nspb_noc_bcms),
	.voters = nspb_noc_voters,
	.num_voters = ARRAY_SIZE(nspb_noc_voters),
};

static struct qcom_icc_bcm *pcie_anoc_bcms[] = {
	&bcm_pci0,
};

static struct qcom_icc_node *pcie_anoc_nodes[] = {
	[MASTER_PCIE_0] = &xm_pcie3_0,
	[MASTER_PCIE_1] = &xm_pcie3_1,
	[SLAVE_ANOC_PCIE_GEM_NOC] = &qns_pcie_mem_noc,
};

static char *pcie_anoc_voters[] = {
	[VOTER_IDX_HLOS] = "hlos",
};

static struct qcom_icc_desc lemans_pcie_anoc = {
	.config = &icc_regmap_config,
	.nodes = pcie_anoc_nodes,
	.num_nodes = ARRAY_SIZE(pcie_anoc_nodes),
	.bcms = pcie_anoc_bcms,
	.num_bcms = ARRAY_SIZE(pcie_anoc_bcms),
	.voters = pcie_anoc_voters,
	.num_voters = ARRAY_SIZE(pcie_anoc_voters),
};

static struct qcom_icc_bcm *system_noc_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn3,
	&bcm_sn4,
	&bcm_sn9,
};

static struct qcom_icc_node *system_noc_nodes[] = {
	[MASTER_GIC_AHB] = &qhm_gic,
	[MASTER_A1NOC_SNOC] = &qnm_aggre1_noc,
	[MASTER_A2NOC_SNOC] = &qnm_aggre2_noc,
	[MASTER_LPASS_ANOC] = &qnm_lpass_noc,
	[MASTER_SNOC_CFG] = &qnm_snoc_cfg,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_GIC] = &xm_gic,
	[SLAVE_SNOC_GEM_NOC_GC] = &qns_gemnoc_gc,
	[SLAVE_SNOC_GEM_NOC_SF] = &qns_gemnoc_sf,
	[SLAVE_SERVICE_SNOC] = &srvc_snoc,
};

static char *system_noc_voters[] = {
	[VOTER_IDX_HLOS] = "hlos",
};

static struct qcom_icc_desc lemans_system_noc = {
	.config = &icc_regmap_config,
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
	.voters = system_noc_voters,
	.num_voters = ARRAY_SIZE(system_noc_voters),
};

static int qnoc_probe(struct platform_device *pdev)
{
	int ret;

	ret = qcom_icc_rpmh_probe(pdev);
	if (ret)
		dev_err(&pdev->dev, "failed to register ICC provider: %d\n", ret);
	else
		dev_info(&pdev->dev, "Registered LEMANS ICC\n");

	return ret;
}

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,lemans-aggre1_noc",
	  .data = &lemans_aggre1_noc},
	{ .compatible = "qcom,lemans-aggre2_noc",
	  .data = &lemans_aggre2_noc},
	{ .compatible = "qcom,lemans-clk_virt",
	  .data = &lemans_clk_virt},
	{ .compatible = "qcom,lemans-config_noc",
	  .data = &lemans_config_noc},
	{ .compatible = "qcom,lemans-dc_noc",
	  .data = &lemans_dc_noc},
	{ .compatible = "qcom,lemans-gem_noc",
	  .data = &lemans_gem_noc},
	{ .compatible = "qcom,lemans-gpdsp_anoc",
	  .data = &lemans_gpdsp_anoc},
	{ .compatible = "qcom,lemans-lpass_ag_noc",
	  .data = &lemans_lpass_ag_noc},
	{ .compatible = "qcom,lemans-mc_virt",
	  .data = &lemans_mc_virt},
	{ .compatible = "qcom,lemans-mmss_noc",
	  .data = &lemans_mmss_noc},
	{ .compatible = "qcom,lemans-nspa_noc",
	  .data = &lemans_nspa_noc},
	{ .compatible = "qcom,lemans-nspb_noc",
	  .data = &lemans_nspb_noc},
	{ .compatible = "qcom,lemans-pcie_anoc",
	  .data = &lemans_pcie_anoc},
	{ .compatible = "qcom,lemans-system_noc",
	  .data = &lemans_system_noc},
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qnoc_probe,
	.remove = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-lemans",
		.of_match_table = qnoc_of_match,
		.sync_state = qcom_icc_rpmh_sync_state,
	},
};

static int __init qnoc_driver_init(void)
{
	return platform_driver_register(&qnoc_driver);
}
core_initcall(qnoc_driver_init);

MODULE_DESCRIPTION("Lemans NoC driver");
MODULE_LICENSE("GPL");
