// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <dt-bindings/interconnect/qcom,mdm9x07.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/soc/qcom/smd-rpm.h>
#include <soc/qcom/rpm-smd.h>

#include "icc-rpm.h"
#include "qnoc-qos-rpm.h"
#include "rpm-ids.h"

static LIST_HEAD(qnoc_probe_list);
static DEFINE_MUTEX(probe_list_lock);

static int probe_count;

static const struct clk_bulk_data bus_clocks[] = {
	{ .id = "bus" },
	{ .id = "bus_a" },
};

static struct qcom_icc_qosbox apps_proc_qos = {
	.regs = icc_bimc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x7300 },
	.config = &(struct qos_config) {
		.prio = 0,
		.bke_enable = 1,
	 },
};

static struct qcom_icc_node apps_proc = {
	.name = "apps_proc",
	.id = MASTER_AMPSS_M0,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_bimc_ops,
	.qosbox = &apps_proc_qos,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 2,
	.links = { SLAVE_EBI_CH0, SLAVE_BIMC_PCNOC },
};

static struct qcom_icc_node mas_pcnoc_bimc_1 = {
	.name = "mas_pcnoc_bimc_1",
	.id = MASTER_PCNOC_BIMC_1,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = ICBID_MASTER_PCNOC_BIMC_1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = { SLAVE_EBI_CH0 },
};

static struct qcom_icc_qosbox tcu_0_qos = {
	.regs = icc_bimc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x1B300 },
	.config = &(struct qos_config) {
		.prio = 2,
		.bke_enable = 1,
	 },
};

static struct qcom_icc_node tcu_0 = {
	.name = "tcu_0",
	.id = MASTER_TCU_0,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_bimc_ops,
	.qosbox = &tcu_0_qos,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = { SLAVE_EBI_CH0 },
};

static struct qcom_icc_node qdss_bam = {
	.name = "qdss_bam",
	.id = MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = ICBID_MASTER_QDSS_BAM,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = { SNOC_QDSS_INT },
};

static struct qcom_icc_node mas_bimc_pcnoc = {
	.name = "mas_bimc_pcnoc",
	.id = MASTER_BIMC_PCNOC,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = ICBID_MASTER_BIMC_PCNOC,
	.slv_rpm_id = -1,
	.num_links = 3,
	.links = { PNOC_INT_0, PNOC_INT_2,
		   SLAVE_CATS_128 },
};

static struct qcom_icc_node qdss_etr = {
	.name = "qdss_etr",
	.id = MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = ICBID_MASTER_QDSS_ETR,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = { SNOC_QDSS_INT },
};

static struct qcom_icc_node mas_audio = {
	.name = "mas_audio",
	.id = MASTER_AUDIO,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = ICBID_MASTER_AUDIO,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = { PNOC_M_0 },
};

static struct qcom_icc_node qpic = {
	.name = "qpic",
	.id = MASTER_QPIC,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = ICBID_MASTER_QPIC,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = { PNOC_M_0 },
};

static struct qcom_icc_node mas_hsic = {
	.name = "mas_hsic",
	.id = MASTER_USB_HSIC,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = ICBID_MASTER_USB_HSIC,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = { PNOC_M_0 },
};

static struct qcom_icc_node mas_blsp_1 = {
	.name = "mas_blsp_1",
	.id = MASTER_BLSP_1,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = ICBID_MASTER_BLSP_1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = { PNOC_M_1 },
};

static struct qcom_icc_node usb_hs1 = {
	.name = "usb_hs1",
	.id = MASTER_USB_HS,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = ICBID_MASTER_USB_HS1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = { PNOC_M_1 },
};

static struct qcom_icc_qosbox crypto_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x7000 },
	.config = &(struct qos_config) {
		.prio = 2,
		.bke_enable = 0,
	},
};

static struct qcom_icc_node crypto = {
	.name = "crypto",
	.id = MASTER_CRYPTO_CORE_0,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &crypto_qos,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = { PNOC_INT_3 },
};

static struct qcom_icc_node mas_sdcc_1 = {
	.name = "mas_sdcc_1",
	.id = MASTER_SDCC_1,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = ICBID_MASTER_SDCC_1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = { PNOC_INT_3 },
};

static struct qcom_icc_node mas_sdcc_2 = {
	.name = "mas_sdcc_2",
	.id = MASTER_SDCC_2,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = ICBID_MASTER_SDCC_2,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = { PNOC_INT_3 },
};

static struct qcom_icc_node xi_usb_hs1 = {
	.name = "xi_usb_hs1",
	.id = MASTER_XM_USB_HS1,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = ICBID_MASTER_XI_USB_HS1,
	.slv_rpm_id = -1,
	.num_links = 2,
	.links = { SLAVE_PCNOC_BIMC_1, PNOC_INT_2 },
};

static struct qcom_icc_node xi_hsic = {
	.name = "xi_hsic",
	.id = MASTER_XI_HSIC,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = ICBID_MASTER_XI_HSIC,
	.slv_rpm_id = -1,
	.num_links = 2,
	.links = { SLAVE_PCNOC_BIMC_1, PNOC_INT_2 },
};

static struct qcom_icc_qosbox mas_sgmii_qos = {
	.regs = icc_qnoc_qos_regs[ICC_QNOC_QOSGEN_TYPE_RPMH],
	.num_ports = 1,
	.offsets = { 0x11000 },
	.config = &(struct qos_config) {
		.prio = 1,
		.bke_enable = 0,
	},
};

static struct qcom_icc_node mas_sgmii = {
	.name = "mas_sgmii",
	.id = MASTER_SGMII,
	.channels = 1,
	.buswidth = 8,
	.noc_ops = &qcom_qnoc4_ops,
	.qosbox = &mas_sgmii_qos,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 2,
	.links = { SLAVE_PCNOC_BIMC_1, PNOC_INT_2 },
};

static struct qcom_icc_node pcnoc_m_0 = {
	.name = "pcnoc_m_0",
	.id = PNOC_M_0,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = ICBID_MASTER_PCNOC_M_0,
	.slv_rpm_id = -1,
	.num_links = 2,
	.links = { PNOC_INT_2, SLAVE_PCNOC_BIMC_1 },
};

static struct qcom_icc_node pcnoc_m_1 = {
	.name = "pcnoc_m_1",
	.id = PNOC_M_1,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = ICBID_MASTER_PCNOC_M_1,
	.slv_rpm_id = -1,
	.num_links = 2,
	.links = { PNOC_INT_2, SLAVE_PCNOC_BIMC_1 },
};

static struct qcom_icc_node qdss_int = {
	.name = "qdss_int",
	.id = SNOC_QDSS_INT,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = ICBID_MASTER_QDSS_INT,
	.slv_rpm_id = -1,
	.num_links = 3,
	.links = { PNOC_INT_0, SLAVE_PCNOC_BIMC_1,
		   PNOC_INT_2 },
};

static struct qcom_icc_node pcnoc_int_0 = {
	.name = "pcnoc_int_0",
	.id = PNOC_INT_0,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 2,
	.links = { SLAVE_OCIMEM, SLAVE_QDSS_STM },
};

static struct qcom_icc_node pcnoc_int_2 = {
	.name = "pcnoc_int_2",
	.id = PNOC_INT_2,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = ICBID_MASTER_PCNOC_INT_2,
	.slv_rpm_id = -1,
	.num_links = 7,
	.links = { PNOC_SLV_0, PNOC_SLV_1,
		   PNOC_SLV_2, PNOC_SLV_3,
		   PNOC_SLV_4, PNOC_SLV_5,
		   SLAVE_TCU },
};

static struct qcom_icc_node pcnoc_int_3 = {
	.name = "pcnoc_int_3",
	.id = PNOC_INT_3,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = ICBID_MASTER_PCNOC_INT_3,
	.slv_rpm_id = -1,
	.num_links = 2,
	.links = { PNOC_INT_2, SLAVE_PCNOC_BIMC_1 },
};

static struct qcom_icc_node pcnoc_s_0 = {
	.name = "pcnoc_s_0",
	.id = PNOC_SLV_0,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = ICBID_MASTER_PCNOC_S_0,
	.slv_rpm_id = -1,
	.num_links = 4,
	.links = { SLAVE_BLSP_1, SLAVE_TCSR,
		   SLAVE_SDCC_1, SLAVE_SGMII },
};

static struct qcom_icc_node pcnoc_s_1 = {
	.name = "pcnoc_s_1",
	.id = PNOC_SLV_1,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = ICBID_MASTER_PCNOC_S_1,
	.slv_rpm_id = -1,
	.num_links = 5,
	.links = { SLAVE_CRYPTO_0_CFG, SLAVE_USB_HS,
		   SLAVE_MESSAGE_RAM, SLAVE_PDM,
		   SLAVE_PRNG },
};

static struct qcom_icc_node pcnoc_s_2 = {
	.name = "pcnoc_s_2",
	.id = PNOC_SLV_2,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = ICBID_MASTER_PCNOC_S_2,
	.slv_rpm_id = -1,
	.num_links = 3,
	.links = { SLAVE_SDCC_2, SLAVE_AUDIO,
		   SLAVE_USB_HSIC },
};

static struct qcom_icc_node pcnoc_s_3 = {
	.name = "pcnoc_s_3",
	.id = PNOC_SLV_3,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 2,
	.links = { SLAVE_QDSS_CFG, SLAVE_USB_PHYS_CFG },
};

static struct qcom_icc_node pcnoc_s_4 = {
	.name = "pcnoc_s_4",
	.id = PNOC_SLV_4,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = ICBID_MASTER_PCNOC_S_4,
	.slv_rpm_id = -1,
	.num_links = 2,
	.links = { SLAVE_IMEM_CFG, SLAVE_PMIC_ARB },
};

static struct qcom_icc_node pcnoc_s_5 = {
	.name = "pcnoc_s_5",
	.id = PNOC_SLV_5,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = ICBID_MASTER_PCNOC_S_5,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = { SLAVE_TLMM },
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.id = SLAVE_EBI_CH0,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = ICBID_SLAVE_EBI1,
	.num_links = 0,
};

static struct qcom_icc_node slv_bimc_pcnoc = {
	.name = "slv_bimc_pcnoc",
	.id = SLAVE_BIMC_PCNOC,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = ICBID_SLAVE_BIMC_PCNOC,
	.num_links = 1,
	.links = { MASTER_BIMC_PCNOC },
};

static struct qcom_icc_node slv_pcnoc_bimc_1 = {
	.name = "slv_pcnoc_bimc_1",
	.id = SLAVE_PCNOC_BIMC_1,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = ICBID_SLAVE_PCNOC_BIMC_1,
	.num_links = 1,
	.links = { MASTER_PCNOC_BIMC_1 },
};

static struct qcom_icc_node imem = {
	.name = "imem",
	.id = SLAVE_OCIMEM,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qdss_stm = {
	.name = "qdss_stm",
	.id = SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node cats_0 = {
	.name = "cats_0",
	.id = SLAVE_CATS_128,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node tcsr = {
	.name = "tcsr",
	.id = SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = ICBID_SLAVE_TCSR,
	.num_links = 0,
};

static struct qcom_icc_node slv_sdcc_1 = {
	.name = "sdcc_1",
	.id = SLAVE_SDCC_1,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = ICBID_SLAVE_SDCC_1,
	.num_links = 0,
};

static struct qcom_icc_node slv_blsp_1 = {
	.name = "slv_blsp_1",
	.id = SLAVE_BLSP_1,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = ICBID_SLAVE_BLSP_1,
	.num_links = 0,
};

static struct qcom_icc_node slv_sgmii = {
	.name = "slv_sgmii",
	.id = SLAVE_SGMII,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node crypto_0_cfg = {
	.name = "crypto_0_cfg",
	.id = SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node message_ram = {
	.name = "message_ram",
	.id = SLAVE_MESSAGE_RAM,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = ICBID_SLAVE_MESSAGE_RAM,
	.num_links = 0,
};

static struct qcom_icc_node pdm = {
	.name = "pdm",
	.id = SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = ICBID_SLAVE_PDM,
	.num_links = 0,
};

static struct qcom_icc_node prng = {
	.name = "prng",
	.id = SLAVE_PRNG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = ICBID_SLAVE_PRNG,
	.num_links = 0,
};

static struct qcom_icc_node usb2 = {
	.name = "usb2",
	.id = SLAVE_USB_HS,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node slv_sdcc_2 = {
	.name = "slv_sdcc_2",
	.id = SLAVE_SDCC_2,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = ICBID_SLAVE_SDCC_2,
	.num_links = 0,
};

static struct qcom_icc_node slv_audio = {
	.name = "slv_audio",
	.id = SLAVE_AUDIO,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = ICBID_SLAVE_AUDIO,
	.num_links = 0,
};

static struct qcom_icc_node slv_hsic = {
	.name = "slv_hsic",
	.id = SLAVE_USB_HSIC,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = ICBID_SLAVE_USB_HSIC,
	.num_links = 0,
};

static struct qcom_icc_node qdss_cfg = {
	.name = "qdss_cfg",
	.id = SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node usb_phy = {
	.name = "usb_phy",
	.id = SLAVE_USB_PHYS_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node tlmm = {
	.name = "tlmm",
	.id = SLAVE_TLMM,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = ICBID_SLAVE_TLMM,
	.num_links = 0,
};

static struct qcom_icc_node imem_cfg = {
	.name = "imem_cfg",
	.id = SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node pmic_arb = {
	.name = "pmic_arb",
	.id = SLAVE_PMIC_ARB,
	.channels = 1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = ICBID_SLAVE_PMIC_ARB,
	.num_links = 0,
};

static struct qcom_icc_node tcu = {
	.name = "tcu",
	.id = SLAVE_TCU,
	.channels = 1,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node *bimc_nodes[] = {
	[MASTER_AMPSS_M0] = &apps_proc,
	[MASTER_PCNOC_BIMC_1] = &mas_pcnoc_bimc_1,
	[MASTER_TCU_0] = &tcu_0,
	[SLAVE_EBI_CH0] = &ebi,
	[SLAVE_BIMC_PCNOC] = &slv_bimc_pcnoc,
};

static struct qcom_icc_desc mdm9x07_bimc = {
	.nodes = bimc_nodes,
	.num_nodes = ARRAY_SIZE(bimc_nodes),
};

static struct qcom_icc_node *pcnoc_nodes[] = {
	[MASTER_QDSS_BAM] = &qdss_bam,
	[MASTER_BIMC_PCNOC] = &mas_bimc_pcnoc,
	[MASTER_QDSS_ETR] = &qdss_etr,
	[MASTER_AUDIO] = &mas_audio,
	[MASTER_QPIC] = &qpic,
	[MASTER_USB_HSIC] = &mas_hsic,
	[MASTER_BLSP_1] = &mas_blsp_1,
	[MASTER_USB_HS] = &usb_hs1,
	[MASTER_CRYPTO_CORE_0] = &crypto,
	[MASTER_SDCC_1] = &mas_sdcc_1,
	[MASTER_SDCC_2] = &mas_sdcc_2,
	[MASTER_XM_USB_HS1] = &xi_usb_hs1,
	[MASTER_XI_HSIC] = &xi_hsic,
	[MASTER_SGMII] = &mas_sgmii,
	[PNOC_M_0] = &pcnoc_m_0,
	[PNOC_M_1] = &pcnoc_m_1,
	[SNOC_QDSS_INT] = &qdss_int,
	[PNOC_INT_0] = &pcnoc_int_0,
	[PNOC_INT_2] = &pcnoc_int_2,
	[PNOC_INT_3] = &pcnoc_int_3,
	[SLAVE_PCNOC_BIMC_1] = &slv_pcnoc_bimc_1,
	[SLAVE_OCIMEM] = &imem,
	[SLAVE_QDSS_STM] = &qdss_stm,
	[SLAVE_CATS_128] = &cats_0,
	[SLAVE_TCSR] = &tcsr,
	[SLAVE_SDCC_1] = &slv_sdcc_1,
	[SLAVE_BLSP_1] = &slv_blsp_1,
	[SLAVE_SGMII] = &slv_sgmii,
	[SLAVE_CRYPTO_0_CFG] = &crypto_0_cfg,
	[SLAVE_MESSAGE_RAM] = &message_ram,
	[SLAVE_PDM] = &pdm,
	[SLAVE_PRNG] = &prng,
	[SLAVE_USB_HS] = &usb2,
	[SLAVE_SDCC_2] = &slv_sdcc_2,
	[SLAVE_AUDIO] = &slv_audio,
	[SLAVE_USB_HSIC] = &slv_hsic,
	[SLAVE_QDSS_CFG] = &qdss_cfg,
	[SLAVE_USB_PHYS_CFG] = &usb_phy,
	[SLAVE_TLMM] = &tlmm,
	[SLAVE_IMEM_CFG] = &imem_cfg,
	[SLAVE_PMIC_ARB] = &pmic_arb,
	[SLAVE_TCU] = &tcu,
	[PNOC_SLV_0] = &pcnoc_s_0,
	[PNOC_SLV_1] = &pcnoc_s_1,
	[PNOC_SLV_2] = &pcnoc_s_2,
	[PNOC_SLV_3] = &pcnoc_s_3,
	[PNOC_SLV_4] = &pcnoc_s_4,
	[PNOC_SLV_5] = &pcnoc_s_5,
};

static struct qcom_icc_desc mdm9x07_pcnoc = {
	.nodes = pcnoc_nodes,
	.num_nodes = ARRAY_SIZE(pcnoc_nodes),
};

static const struct regmap_config icc_regmap_config = {
	.reg_bits       = 32,
	.reg_stride     = 4,
	.val_bits       = 32,
};

static struct regmap *
qcom_icc_map(struct platform_device *pdev, const struct qcom_icc_desc *desc)
{
	void __iomem *base;
	struct resource *res;
	struct device *dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return NULL;

	base = devm_ioremap(dev, res->start, resource_size(res));
	if (IS_ERR(base))
		return ERR_CAST(base);

	return devm_regmap_init_mmio(dev, base, &icc_regmap_config);
}

static int qnoc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct qcom_icc_desc *desc;
	struct icc_onecell_data *data;
	struct icc_provider *provider;
	struct qcom_icc_node **qnodes;
	struct qcom_icc_provider *qp;
	struct icc_node *node, *tmp;
	size_t num_nodes, i;
	int ret;

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -EINVAL;

	qnodes = desc->nodes;
	num_nodes = desc->num_nodes;

	qp = devm_kzalloc(dev, sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return -ENOMEM;

	data = devm_kzalloc(dev, struct_size(data, nodes, num_nodes),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	qp->bus_clks = devm_kmemdup(dev, bus_clocks, sizeof(bus_clocks),
				    GFP_KERNEL);
	if (!qp->bus_clks)
		return -ENOMEM;

	qp->num_clks = ARRAY_SIZE(bus_clocks);
	ret = devm_clk_bulk_get(dev, qp->num_clks, qp->bus_clks);
	if (ret)
		return ret;

	qp->num_qos_clks = devm_clk_bulk_get_all(dev, &qp->qos_clks);
	if (qp->num_qos_clks < 0)
		return qp->num_qos_clks;

	ret = clk_bulk_prepare_enable(qp->num_clks, qp->bus_clks);
	if (ret)
		return ret;

	provider = &qp->provider;
	provider->dev = dev;
	provider->set = qcom_icc_rpm_set;
	provider->pre_aggregate = qcom_icc_rpm_pre_aggregate;
	provider->aggregate = qcom_icc_rpm_aggregate;
	provider->get_bw = qcom_icc_get_bw_stub;
	provider->xlate = of_icc_xlate_onecell;
	INIT_LIST_HEAD(&provider->nodes);
	provider->data = data;
	qp->dev = &pdev->dev;

	qp->init = true;
	qp->keepalive = of_property_read_bool(dev->of_node, "qcom,keepalive");

	if (of_property_read_u32(dev->of_node, "qcom,util-factor",
				 &qp->util_factor))
		qp->util_factor = DEFAULT_UTIL_FACTOR;

	qp->regmap = qcom_icc_map(pdev, desc);
	if (IS_ERR(qp->regmap))
		return PTR_ERR(qp->regmap);

	ret = icc_provider_add(provider);
	if (ret) {
		dev_err(dev, "error adding interconnect provider: %d\n", ret);
		clk_bulk_disable_unprepare(qp->num_clks, qp->bus_clks);
		return ret;
	}

	ret = clk_bulk_prepare_enable(qp->num_qos_clks, qp->qos_clks);
	if (ret)
		goto disable_clks;

	for (i = 0; i < num_nodes; i++) {
		size_t j;

		if (!qnodes[i])
			continue;

		qnodes[i]->regmap = dev_get_regmap(qp->dev, NULL);

		node = icc_node_create(qnodes[i]->id);
		if (IS_ERR(node)) {
			ret = PTR_ERR(node);
			goto err;
		}

		if (qnodes[i]->qosbox) {
			qnodes[i]->noc_ops->set_qos(qnodes[i]);
			qnodes[i]->qosbox->initialized = true;
		}

		node->name = qnodes[i]->name;
		node->data = qnodes[i];
		icc_node_add(node, provider);

		for (j = 0; j < qnodes[i]->num_links; j++)
			icc_link_create(node, qnodes[i]->links[j]);

		data->nodes[i] = node;
	}
	data->num_nodes = num_nodes;

	clk_bulk_disable_unprepare(qp->num_qos_clks, qp->qos_clks);
	platform_set_drvdata(pdev, qp);

	dev_info(dev, "Registered Mdm9x07 ICC\n");

	mutex_lock(&probe_list_lock);
	list_add_tail(&qp->probe_list, &qnoc_probe_list);
	mutex_unlock(&probe_list_lock);

	return 0;
err:
	list_for_each_entry_safe(node, tmp, &provider->nodes, node_list) {
		icc_node_del(node);
		icc_node_destroy(node->id);
	}
	clk_bulk_disable_unprepare(qp->num_qos_clks, qp->qos_clks);

disable_clks:
	clk_bulk_disable_unprepare(qp->num_clks, qp->bus_clks);
	icc_provider_del(provider);
	return ret;
}

static int qnoc_remove(struct platform_device *pdev)
{
	struct qcom_icc_provider *qp = platform_get_drvdata(pdev);
	struct icc_provider *provider = &qp->provider;
	struct icc_node *n, *tmp;

	list_for_each_entry_safe(n, tmp, &provider->nodes, node_list) {
		icc_node_del(n);
		icc_node_destroy(n->id);
	}

	clk_bulk_disable_unprepare(qp->num_clks, qp->bus_clks);

	icc_provider_del(provider);
	return 0;
}

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,mdm9x07-bimc",
	  .data = &mdm9x07_bimc},
	{ .compatible = "qcom,mdm9x07-pcnoc",
	  .data = &mdm9x07_pcnoc},
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static void qnoc_sync_state(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct qcom_icc_provider *qp = platform_get_drvdata(pdev);
	int ret = 0, i;

	mutex_lock(&probe_list_lock);
	probe_count++;

	if (probe_count < ARRAY_SIZE(qnoc_of_match) - 1) {
		mutex_unlock(&probe_list_lock);
		return;
	}

	list_for_each_entry(qp, &qnoc_probe_list, probe_list) {
		qp->init = false;

		if (!qp->keepalive)
			continue;

		for (i = 0; i < RPM_NUM_CXT; i++) {
			if (i == RPM_ACTIVE_CXT) {
				if (qp->bus_clk_cur_rate[i] == 0)
					ret = clk_set_rate(qp->bus_clks[i].clk,
						RPM_CLK_MIN_LEVEL);
				else
					ret = clk_set_rate(qp->bus_clks[i].clk,
						qp->bus_clk_cur_rate[i]);
			} else {
				ret = clk_set_rate(qp->bus_clks[i].clk,
					qp->bus_clk_cur_rate[i]);
			}

			if (ret)
				pr_err("%s clk_set_rate error: %d\n",
					qp->bus_clks[i].id, ret);
		}
	}

	mutex_unlock(&probe_list_lock);

	pr_err("Mdm9x07 ICC Sync State done\n");
}

static struct platform_driver qnoc_driver = {
	.probe = qnoc_probe,
	.remove = qnoc_remove,
	.driver = {
		.name = "qnoc-mdm9x07",
		.of_match_table = qnoc_of_match,
		.sync_state = qnoc_sync_state,
	},
};

static int __init qnoc_driver_init(void)
{
	return platform_driver_register(&qnoc_driver);
}
subsys_initcall(qnoc_driver_init);

MODULE_DESCRIPTION("Mdm9x07 NoC driver");
MODULE_LICENSE("GPL");
