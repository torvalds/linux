// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Linaro Ltd
 */

#include <dt-bindings/interconnect/qcom,qcs404.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>


#include "smd-rpm.h"
#include "icc-rpm.h"

enum {
	QCS404_MASTER_AMPSS_M0 = 1,
	QCS404_MASTER_GRAPHICS_3D,
	QCS404_MASTER_MDP_PORT0,
	QCS404_SNOC_BIMC_1_MAS,
	QCS404_MASTER_TCU_0,
	QCS404_MASTER_SPDM,
	QCS404_MASTER_BLSP_1,
	QCS404_MASTER_BLSP_2,
	QCS404_MASTER_XM_USB_HS1,
	QCS404_MASTER_CRYPTO_CORE0,
	QCS404_MASTER_SDCC_1,
	QCS404_MASTER_SDCC_2,
	QCS404_SNOC_PNOC_MAS,
	QCS404_MASTER_QPIC,
	QCS404_MASTER_QDSS_BAM,
	QCS404_BIMC_SNOC_MAS,
	QCS404_PNOC_SNOC_MAS,
	QCS404_MASTER_QDSS_ETR,
	QCS404_MASTER_EMAC,
	QCS404_MASTER_PCIE,
	QCS404_MASTER_USB3,
	QCS404_PNOC_INT_0,
	QCS404_PNOC_INT_2,
	QCS404_PNOC_INT_3,
	QCS404_PNOC_SLV_0,
	QCS404_PNOC_SLV_1,
	QCS404_PNOC_SLV_2,
	QCS404_PNOC_SLV_3,
	QCS404_PNOC_SLV_4,
	QCS404_PNOC_SLV_6,
	QCS404_PNOC_SLV_7,
	QCS404_PNOC_SLV_8,
	QCS404_PNOC_SLV_9,
	QCS404_PNOC_SLV_10,
	QCS404_PNOC_SLV_11,
	QCS404_SNOC_QDSS_INT,
	QCS404_SNOC_INT_0,
	QCS404_SNOC_INT_1,
	QCS404_SNOC_INT_2,
	QCS404_SLAVE_EBI_CH0,
	QCS404_BIMC_SNOC_SLV,
	QCS404_SLAVE_SPDM_WRAPPER,
	QCS404_SLAVE_PDM,
	QCS404_SLAVE_PRNG,
	QCS404_SLAVE_TCSR,
	QCS404_SLAVE_SNOC_CFG,
	QCS404_SLAVE_MESSAGE_RAM,
	QCS404_SLAVE_DISPLAY_CFG,
	QCS404_SLAVE_GRAPHICS_3D_CFG,
	QCS404_SLAVE_BLSP_1,
	QCS404_SLAVE_TLMM_NORTH,
	QCS404_SLAVE_PCIE_1,
	QCS404_SLAVE_EMAC_CFG,
	QCS404_SLAVE_BLSP_2,
	QCS404_SLAVE_TLMM_EAST,
	QCS404_SLAVE_TCU,
	QCS404_SLAVE_PMIC_ARB,
	QCS404_SLAVE_SDCC_1,
	QCS404_SLAVE_SDCC_2,
	QCS404_SLAVE_TLMM_SOUTH,
	QCS404_SLAVE_USB_HS,
	QCS404_SLAVE_USB3,
	QCS404_SLAVE_CRYPTO_0_CFG,
	QCS404_PNOC_SNOC_SLV,
	QCS404_SLAVE_APPSS,
	QCS404_SLAVE_WCSS,
	QCS404_SNOC_BIMC_1_SLV,
	QCS404_SLAVE_OCIMEM,
	QCS404_SNOC_PNOC_SLV,
	QCS404_SLAVE_QDSS_STM,
	QCS404_SLAVE_CATS_128,
	QCS404_SLAVE_OCMEM_64,
	QCS404_SLAVE_LPASS,
};

static const u16 mas_apps_proc_links[] = {
	QCS404_SLAVE_EBI_CH0,
	QCS404_BIMC_SNOC_SLV
};

static struct qcom_icc_node mas_apps_proc = {
	.name = "mas_apps_proc",
	.id = QCS404_MASTER_AMPSS_M0,
	.buswidth = 8,
	.mas_rpm_id = 0,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_apps_proc_links),
	.links = mas_apps_proc_links,
};

static const u16 mas_oxili_links[] = {
	QCS404_SLAVE_EBI_CH0,
	QCS404_BIMC_SNOC_SLV
};

static struct qcom_icc_node mas_oxili = {
	.name = "mas_oxili",
	.id = QCS404_MASTER_GRAPHICS_3D,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_oxili_links),
	.links = mas_oxili_links,
};

static const u16 mas_mdp_links[] = {
	QCS404_SLAVE_EBI_CH0,
	QCS404_BIMC_SNOC_SLV
};

static struct qcom_icc_node mas_mdp = {
	.name = "mas_mdp",
	.id = QCS404_MASTER_MDP_PORT0,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_mdp_links),
	.links = mas_mdp_links,
};

static const u16 mas_snoc_bimc_1_links[] = {
	QCS404_SLAVE_EBI_CH0
};

static struct qcom_icc_node mas_snoc_bimc_1 = {
	.name = "mas_snoc_bimc_1",
	.id = QCS404_SNOC_BIMC_1_MAS,
	.buswidth = 8,
	.mas_rpm_id = 76,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_snoc_bimc_1_links),
	.links = mas_snoc_bimc_1_links,
};

static const u16 mas_tcu_0_links[] = {
	QCS404_SLAVE_EBI_CH0,
	QCS404_BIMC_SNOC_SLV
};

static struct qcom_icc_node mas_tcu_0 = {
	.name = "mas_tcu_0",
	.id = QCS404_MASTER_TCU_0,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_tcu_0_links),
	.links = mas_tcu_0_links,
};

static const u16 mas_spdm_links[] = {
	QCS404_PNOC_INT_3
};

static struct qcom_icc_node mas_spdm = {
	.name = "mas_spdm",
	.id = QCS404_MASTER_SPDM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_spdm_links),
	.links = mas_spdm_links,
};

static const u16 mas_blsp_1_links[] = {
	QCS404_PNOC_INT_3
};

static struct qcom_icc_node mas_blsp_1 = {
	.name = "mas_blsp_1",
	.id = QCS404_MASTER_BLSP_1,
	.buswidth = 4,
	.mas_rpm_id = 41,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_blsp_1_links),
	.links = mas_blsp_1_links,
};

static const u16 mas_blsp_2_links[] = {
	QCS404_PNOC_INT_3
};

static struct qcom_icc_node mas_blsp_2 = {
	.name = "mas_blsp_2",
	.id = QCS404_MASTER_BLSP_2,
	.buswidth = 4,
	.mas_rpm_id = 39,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_blsp_2_links),
	.links = mas_blsp_2_links,
};

static const u16 mas_xi_usb_hs1_links[] = {
	QCS404_PNOC_INT_0
};

static struct qcom_icc_node mas_xi_usb_hs1 = {
	.name = "mas_xi_usb_hs1",
	.id = QCS404_MASTER_XM_USB_HS1,
	.buswidth = 8,
	.mas_rpm_id = 138,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_xi_usb_hs1_links),
	.links = mas_xi_usb_hs1_links,
};

static const u16 mas_crypto_links[] = {
	QCS404_PNOC_SNOC_SLV,
	QCS404_PNOC_INT_2
};

static struct qcom_icc_node mas_crypto = {
	.name = "mas_crypto",
	.id = QCS404_MASTER_CRYPTO_CORE0,
	.buswidth = 8,
	.mas_rpm_id = 23,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_crypto_links),
	.links = mas_crypto_links,
};

static const u16 mas_sdcc_1_links[] = {
	QCS404_PNOC_INT_0
};

static struct qcom_icc_node mas_sdcc_1 = {
	.name = "mas_sdcc_1",
	.id = QCS404_MASTER_SDCC_1,
	.buswidth = 8,
	.mas_rpm_id = 33,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_sdcc_1_links),
	.links = mas_sdcc_1_links,
};

static const u16 mas_sdcc_2_links[] = {
	QCS404_PNOC_INT_0
};

static struct qcom_icc_node mas_sdcc_2 = {
	.name = "mas_sdcc_2",
	.id = QCS404_MASTER_SDCC_2,
	.buswidth = 8,
	.mas_rpm_id = 35,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_sdcc_2_links),
	.links = mas_sdcc_2_links,
};

static const u16 mas_snoc_pcnoc_links[] = {
	QCS404_PNOC_INT_2
};

static struct qcom_icc_node mas_snoc_pcnoc = {
	.name = "mas_snoc_pcnoc",
	.id = QCS404_SNOC_PNOC_MAS,
	.buswidth = 8,
	.mas_rpm_id = 77,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_snoc_pcnoc_links),
	.links = mas_snoc_pcnoc_links,
};

static const u16 mas_qpic_links[] = {
	QCS404_PNOC_INT_0
};

static struct qcom_icc_node mas_qpic = {
	.name = "mas_qpic",
	.id = QCS404_MASTER_QPIC,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_qpic_links),
	.links = mas_qpic_links,
};

static const u16 mas_qdss_bam_links[] = {
	QCS404_SNOC_QDSS_INT
};

static struct qcom_icc_node mas_qdss_bam = {
	.name = "mas_qdss_bam",
	.id = QCS404_MASTER_QDSS_BAM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_qdss_bam_links),
	.links = mas_qdss_bam_links,
};

static const u16 mas_bimc_snoc_links[] = {
	QCS404_SLAVE_OCMEM_64,
	QCS404_SLAVE_CATS_128,
	QCS404_SNOC_INT_0,
	QCS404_SNOC_INT_1
};

static struct qcom_icc_node mas_bimc_snoc = {
	.name = "mas_bimc_snoc",
	.id = QCS404_BIMC_SNOC_MAS,
	.buswidth = 8,
	.mas_rpm_id = 21,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_bimc_snoc_links),
	.links = mas_bimc_snoc_links,
};

static const u16 mas_pcnoc_snoc_links[] = {
	QCS404_SNOC_BIMC_1_SLV,
	QCS404_SNOC_INT_2,
	QCS404_SNOC_INT_0
};

static struct qcom_icc_node mas_pcnoc_snoc = {
	.name = "mas_pcnoc_snoc",
	.id = QCS404_PNOC_SNOC_MAS,
	.buswidth = 8,
	.mas_rpm_id = 29,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_pcnoc_snoc_links),
	.links = mas_pcnoc_snoc_links,
};

static const u16 mas_qdss_etr_links[] = {
	QCS404_SNOC_QDSS_INT
};

static struct qcom_icc_node mas_qdss_etr = {
	.name = "mas_qdss_etr",
	.id = QCS404_MASTER_QDSS_ETR,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_qdss_etr_links),
	.links = mas_qdss_etr_links,
};

static const u16 mas_emac_links[] = {
	QCS404_SNOC_BIMC_1_SLV,
	QCS404_SNOC_INT_1
};

static struct qcom_icc_node mas_emac = {
	.name = "mas_emac",
	.id = QCS404_MASTER_EMAC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_emac_links),
	.links = mas_emac_links,
};

static const u16 mas_pcie_links[] = {
	QCS404_SNOC_BIMC_1_SLV,
	QCS404_SNOC_INT_1
};

static struct qcom_icc_node mas_pcie = {
	.name = "mas_pcie",
	.id = QCS404_MASTER_PCIE,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_pcie_links),
	.links = mas_pcie_links,
};

static const u16 mas_usb3_links[] = {
	QCS404_SNOC_BIMC_1_SLV,
	QCS404_SNOC_INT_1
};

static struct qcom_icc_node mas_usb3 = {
	.name = "mas_usb3",
	.id = QCS404_MASTER_USB3,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_usb3_links),
	.links = mas_usb3_links,
};

static const u16 pcnoc_int_0_links[] = {
	QCS404_PNOC_SNOC_SLV,
	QCS404_PNOC_INT_2
};

static struct qcom_icc_node pcnoc_int_0 = {
	.name = "pcnoc_int_0",
	.id = QCS404_PNOC_INT_0,
	.buswidth = 8,
	.mas_rpm_id = 85,
	.slv_rpm_id = 114,
	.num_links = ARRAY_SIZE(pcnoc_int_0_links),
	.links = pcnoc_int_0_links,
};

static const u16 pcnoc_int_2_links[] = {
	QCS404_PNOC_SLV_10,
	QCS404_SLAVE_TCU,
	QCS404_PNOC_SLV_11,
	QCS404_PNOC_SLV_2,
	QCS404_PNOC_SLV_3,
	QCS404_PNOC_SLV_0,
	QCS404_PNOC_SLV_1,
	QCS404_PNOC_SLV_6,
	QCS404_PNOC_SLV_7,
	QCS404_PNOC_SLV_4,
	QCS404_PNOC_SLV_8,
	QCS404_PNOC_SLV_9
};

static struct qcom_icc_node pcnoc_int_2 = {
	.name = "pcnoc_int_2",
	.id = QCS404_PNOC_INT_2,
	.buswidth = 8,
	.mas_rpm_id = 124,
	.slv_rpm_id = 184,
	.num_links = ARRAY_SIZE(pcnoc_int_2_links),
	.links = pcnoc_int_2_links,
};

static const u16 pcnoc_int_3_links[] = {
	QCS404_PNOC_SNOC_SLV
};

static struct qcom_icc_node pcnoc_int_3 = {
	.name = "pcnoc_int_3",
	.id = QCS404_PNOC_INT_3,
	.buswidth = 8,
	.mas_rpm_id = 125,
	.slv_rpm_id = 185,
	.num_links = ARRAY_SIZE(pcnoc_int_3_links),
	.links = pcnoc_int_3_links,
};

static const u16 pcnoc_s_0_links[] = {
	QCS404_SLAVE_PRNG,
	QCS404_SLAVE_SPDM_WRAPPER,
	QCS404_SLAVE_PDM
};

static struct qcom_icc_node pcnoc_s_0 = {
	.name = "pcnoc_s_0",
	.id = QCS404_PNOC_SLV_0,
	.buswidth = 4,
	.mas_rpm_id = 89,
	.slv_rpm_id = 118,
	.num_links = ARRAY_SIZE(pcnoc_s_0_links),
	.links = pcnoc_s_0_links,
};

static const u16 pcnoc_s_1_links[] = {
	QCS404_SLAVE_TCSR
};

static struct qcom_icc_node pcnoc_s_1 = {
	.name = "pcnoc_s_1",
	.id = QCS404_PNOC_SLV_1,
	.buswidth = 4,
	.mas_rpm_id = 90,
	.slv_rpm_id = 119,
	.num_links = ARRAY_SIZE(pcnoc_s_1_links),
	.links = pcnoc_s_1_links,
};

static const u16 pcnoc_s_2_links[] = {
	QCS404_SLAVE_GRAPHICS_3D_CFG
};

static struct qcom_icc_node pcnoc_s_2 = {
	.name = "pcnoc_s_2",
	.id = QCS404_PNOC_SLV_2,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(pcnoc_s_2_links),
	.links = pcnoc_s_2_links,
};

static const u16 pcnoc_s_3_links[] = {
	QCS404_SLAVE_MESSAGE_RAM
};

static struct qcom_icc_node pcnoc_s_3 = {
	.name = "pcnoc_s_3",
	.id = QCS404_PNOC_SLV_3,
	.buswidth = 4,
	.mas_rpm_id = 92,
	.slv_rpm_id = 121,
	.num_links = ARRAY_SIZE(pcnoc_s_3_links),
	.links = pcnoc_s_3_links,
};

static const u16 pcnoc_s_4_links[] = {
	QCS404_SLAVE_SNOC_CFG
};

static struct qcom_icc_node pcnoc_s_4 = {
	.name = "pcnoc_s_4",
	.id = QCS404_PNOC_SLV_4,
	.buswidth = 4,
	.mas_rpm_id = 93,
	.slv_rpm_id = 122,
	.num_links = ARRAY_SIZE(pcnoc_s_4_links),
	.links = pcnoc_s_4_links,
};

static const u16 pcnoc_s_6_links[] = {
	QCS404_SLAVE_BLSP_1,
	QCS404_SLAVE_TLMM_NORTH,
	QCS404_SLAVE_EMAC_CFG
};

static struct qcom_icc_node pcnoc_s_6 = {
	.name = "pcnoc_s_6",
	.id = QCS404_PNOC_SLV_6,
	.buswidth = 4,
	.mas_rpm_id = 94,
	.slv_rpm_id = 123,
	.num_links = ARRAY_SIZE(pcnoc_s_6_links),
	.links = pcnoc_s_6_links,
};

static const u16 pcnoc_s_7_links[] = {
	QCS404_SLAVE_TLMM_SOUTH,
	QCS404_SLAVE_DISPLAY_CFG,
	QCS404_SLAVE_SDCC_1,
	QCS404_SLAVE_PCIE_1,
	QCS404_SLAVE_SDCC_2
};

static struct qcom_icc_node pcnoc_s_7 = {
	.name = "pcnoc_s_7",
	.id = QCS404_PNOC_SLV_7,
	.buswidth = 4,
	.mas_rpm_id = 95,
	.slv_rpm_id = 124,
	.num_links = ARRAY_SIZE(pcnoc_s_7_links),
	.links = pcnoc_s_7_links,
};

static const u16 pcnoc_s_8_links[] = {
	QCS404_SLAVE_CRYPTO_0_CFG
};

static struct qcom_icc_node pcnoc_s_8 = {
	.name = "pcnoc_s_8",
	.id = QCS404_PNOC_SLV_8,
	.buswidth = 4,
	.mas_rpm_id = 96,
	.slv_rpm_id = 125,
	.num_links = ARRAY_SIZE(pcnoc_s_8_links),
	.links = pcnoc_s_8_links,
};

static const u16 pcnoc_s_9_links[] = {
	QCS404_SLAVE_BLSP_2,
	QCS404_SLAVE_TLMM_EAST,
	QCS404_SLAVE_PMIC_ARB
};

static struct qcom_icc_node pcnoc_s_9 = {
	.name = "pcnoc_s_9",
	.id = QCS404_PNOC_SLV_9,
	.buswidth = 4,
	.mas_rpm_id = 97,
	.slv_rpm_id = 126,
	.num_links = ARRAY_SIZE(pcnoc_s_9_links),
	.links = pcnoc_s_9_links,
};

static const u16 pcnoc_s_10_links[] = {
	QCS404_SLAVE_USB_HS
};

static struct qcom_icc_node pcnoc_s_10 = {
	.name = "pcnoc_s_10",
	.id = QCS404_PNOC_SLV_10,
	.buswidth = 4,
	.mas_rpm_id = 157,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(pcnoc_s_10_links),
	.links = pcnoc_s_10_links,
};

static const u16 pcnoc_s_11_links[] = {
	QCS404_SLAVE_USB3
};

static struct qcom_icc_node pcnoc_s_11 = {
	.name = "pcnoc_s_11",
	.id = QCS404_PNOC_SLV_11,
	.buswidth = 4,
	.mas_rpm_id = 158,
	.slv_rpm_id = 246,
	.num_links = ARRAY_SIZE(pcnoc_s_11_links),
	.links = pcnoc_s_11_links,
};

static const u16 qdss_int_links[] = {
	QCS404_SNOC_BIMC_1_SLV,
	QCS404_SNOC_INT_1
};

static struct qcom_icc_node qdss_int = {
	.name = "qdss_int",
	.id = QCS404_SNOC_QDSS_INT,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qdss_int_links),
	.links = qdss_int_links,
};

static const u16 snoc_int_0_links[] = {
	QCS404_SLAVE_LPASS,
	QCS404_SLAVE_APPSS,
	QCS404_SLAVE_WCSS
};

static struct qcom_icc_node snoc_int_0 = {
	.name = "snoc_int_0",
	.id = QCS404_SNOC_INT_0,
	.buswidth = 8,
	.mas_rpm_id = 99,
	.slv_rpm_id = 130,
	.num_links = ARRAY_SIZE(snoc_int_0_links),
	.links = snoc_int_0_links,
};

static const u16 snoc_int_1_links[] = {
	QCS404_SNOC_PNOC_SLV,
	QCS404_SNOC_INT_2
};

static struct qcom_icc_node snoc_int_1 = {
	.name = "snoc_int_1",
	.id = QCS404_SNOC_INT_1,
	.buswidth = 8,
	.mas_rpm_id = 100,
	.slv_rpm_id = 131,
	.num_links = ARRAY_SIZE(snoc_int_1_links),
	.links = snoc_int_1_links,
};

static const u16 snoc_int_2_links[] = {
	QCS404_SLAVE_QDSS_STM,
	QCS404_SLAVE_OCIMEM
};

static struct qcom_icc_node snoc_int_2 = {
	.name = "snoc_int_2",
	.id = QCS404_SNOC_INT_2,
	.buswidth = 8,
	.mas_rpm_id = 134,
	.slv_rpm_id = 197,
	.num_links = ARRAY_SIZE(snoc_int_2_links),
	.links = snoc_int_2_links,
};

static struct qcom_icc_node slv_ebi = {
	.name = "slv_ebi",
	.id = QCS404_SLAVE_EBI_CH0,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 0,
};

static const u16 slv_bimc_snoc_links[] = {
	QCS404_BIMC_SNOC_MAS
};

static struct qcom_icc_node slv_bimc_snoc = {
	.name = "slv_bimc_snoc",
	.id = QCS404_BIMC_SNOC_SLV,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 2,
	.num_links = ARRAY_SIZE(slv_bimc_snoc_links),
	.links = slv_bimc_snoc_links,
};

static struct qcom_icc_node slv_spdm = {
	.name = "slv_spdm",
	.id = QCS404_SLAVE_SPDM_WRAPPER,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node slv_pdm = {
	.name = "slv_pdm",
	.id = QCS404_SLAVE_PDM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 41,
};

static struct qcom_icc_node slv_prng = {
	.name = "slv_prng",
	.id = QCS404_SLAVE_PRNG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 44,
};

static struct qcom_icc_node slv_tcsr = {
	.name = "slv_tcsr",
	.id = QCS404_SLAVE_TCSR,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 50,
};

static struct qcom_icc_node slv_snoc_cfg = {
	.name = "slv_snoc_cfg",
	.id = QCS404_SLAVE_SNOC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 70,
};

static struct qcom_icc_node slv_message_ram = {
	.name = "slv_message_ram",
	.id = QCS404_SLAVE_MESSAGE_RAM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 55,
};

static struct qcom_icc_node slv_disp_ss_cfg = {
	.name = "slv_disp_ss_cfg",
	.id = QCS404_SLAVE_DISPLAY_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node slv_gpu_cfg = {
	.name = "slv_gpu_cfg",
	.id = QCS404_SLAVE_GRAPHICS_3D_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node slv_blsp_1 = {
	.name = "slv_blsp_1",
	.id = QCS404_SLAVE_BLSP_1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 39,
};

static struct qcom_icc_node slv_tlmm_north = {
	.name = "slv_tlmm_north",
	.id = QCS404_SLAVE_TLMM_NORTH,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 214,
};

static struct qcom_icc_node slv_pcie = {
	.name = "slv_pcie",
	.id = QCS404_SLAVE_PCIE_1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node slv_ethernet = {
	.name = "slv_ethernet",
	.id = QCS404_SLAVE_EMAC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node slv_blsp_2 = {
	.name = "slv_blsp_2",
	.id = QCS404_SLAVE_BLSP_2,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 37,
};

static struct qcom_icc_node slv_tlmm_east = {
	.name = "slv_tlmm_east",
	.id = QCS404_SLAVE_TLMM_EAST,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 213,
};

static struct qcom_icc_node slv_tcu = {
	.name = "slv_tcu",
	.id = QCS404_SLAVE_TCU,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node slv_pmic_arb = {
	.name = "slv_pmic_arb",
	.id = QCS404_SLAVE_PMIC_ARB,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 59,
};

static struct qcom_icc_node slv_sdcc_1 = {
	.name = "slv_sdcc_1",
	.id = QCS404_SLAVE_SDCC_1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 31,
};

static struct qcom_icc_node slv_sdcc_2 = {
	.name = "slv_sdcc_2",
	.id = QCS404_SLAVE_SDCC_2,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 33,
};

static struct qcom_icc_node slv_tlmm_south = {
	.name = "slv_tlmm_south",
	.id = QCS404_SLAVE_TLMM_SOUTH,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node slv_usb_hs = {
	.name = "slv_usb_hs",
	.id = QCS404_SLAVE_USB_HS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 40,
};

static struct qcom_icc_node slv_usb3 = {
	.name = "slv_usb3",
	.id = QCS404_SLAVE_USB3,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 22,
};

static struct qcom_icc_node slv_crypto_0_cfg = {
	.name = "slv_crypto_0_cfg",
	.id = QCS404_SLAVE_CRYPTO_0_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 52,
};

static const u16 slv_pcnoc_snoc_links[] = {
	QCS404_PNOC_SNOC_MAS
};

static struct qcom_icc_node slv_pcnoc_snoc = {
	.name = "slv_pcnoc_snoc",
	.id = QCS404_PNOC_SNOC_SLV,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 45,
	.num_links = ARRAY_SIZE(slv_pcnoc_snoc_links),
	.links = slv_pcnoc_snoc_links,
};

static struct qcom_icc_node slv_kpss_ahb = {
	.name = "slv_kpss_ahb",
	.id = QCS404_SLAVE_APPSS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node slv_wcss = {
	.name = "slv_wcss",
	.id = QCS404_SLAVE_WCSS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 23,
};

static const u16 slv_snoc_bimc_1_links[] = {
	QCS404_SNOC_BIMC_1_MAS
};

static struct qcom_icc_node slv_snoc_bimc_1 = {
	.name = "slv_snoc_bimc_1",
	.id = QCS404_SNOC_BIMC_1_SLV,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 104,
	.num_links = ARRAY_SIZE(slv_snoc_bimc_1_links),
	.links = slv_snoc_bimc_1_links,
};

static struct qcom_icc_node slv_imem = {
	.name = "slv_imem",
	.id = QCS404_SLAVE_OCIMEM,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 26,
};

static const u16 slv_snoc_pcnoc_links[] = {
	QCS404_SNOC_PNOC_MAS
};

static struct qcom_icc_node slv_snoc_pcnoc = {
	.name = "slv_snoc_pcnoc",
	.id = QCS404_SNOC_PNOC_SLV,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 28,
	.num_links = ARRAY_SIZE(slv_snoc_pcnoc_links),
	.links = slv_snoc_pcnoc_links,
};

static struct qcom_icc_node slv_qdss_stm = {
	.name = "slv_qdss_stm",
	.id = QCS404_SLAVE_QDSS_STM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 30,
};

static struct qcom_icc_node slv_cats_0 = {
	.name = "slv_cats_0",
	.id = QCS404_SLAVE_CATS_128,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node slv_cats_1 = {
	.name = "slv_cats_1",
	.id = QCS404_SLAVE_OCMEM_64,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node slv_lpass = {
	.name = "slv_lpass",
	.id = QCS404_SLAVE_LPASS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node *qcs404_bimc_nodes[] = {
	[MASTER_AMPSS_M0] = &mas_apps_proc,
	[MASTER_OXILI] = &mas_oxili,
	[MASTER_MDP_PORT0] = &mas_mdp,
	[MASTER_SNOC_BIMC_1] = &mas_snoc_bimc_1,
	[MASTER_TCU_0] = &mas_tcu_0,
	[SLAVE_EBI_CH0] = &slv_ebi,
	[SLAVE_BIMC_SNOC] = &slv_bimc_snoc,
};

static struct qcom_icc_desc qcs404_bimc = {
	.nodes = qcs404_bimc_nodes,
	.num_nodes = ARRAY_SIZE(qcs404_bimc_nodes),
};

static struct qcom_icc_node *qcs404_pcnoc_nodes[] = {
	[MASTER_SPDM] = &mas_spdm,
	[MASTER_BLSP_1] = &mas_blsp_1,
	[MASTER_BLSP_2] = &mas_blsp_2,
	[MASTER_XI_USB_HS1] = &mas_xi_usb_hs1,
	[MASTER_CRYPT0] = &mas_crypto,
	[MASTER_SDCC_1] = &mas_sdcc_1,
	[MASTER_SDCC_2] = &mas_sdcc_2,
	[MASTER_SNOC_PCNOC] = &mas_snoc_pcnoc,
	[MASTER_QPIC] = &mas_qpic,
	[PCNOC_INT_0] = &pcnoc_int_0,
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
	[PCNOC_S_9] = &pcnoc_s_9,
	[PCNOC_S_10] = &pcnoc_s_10,
	[PCNOC_S_11] = &pcnoc_s_11,
	[SLAVE_SPDM] = &slv_spdm,
	[SLAVE_PDM] = &slv_pdm,
	[SLAVE_PRNG] = &slv_prng,
	[SLAVE_TCSR] = &slv_tcsr,
	[SLAVE_SNOC_CFG] = &slv_snoc_cfg,
	[SLAVE_MESSAGE_RAM] = &slv_message_ram,
	[SLAVE_DISP_SS_CFG] = &slv_disp_ss_cfg,
	[SLAVE_GPU_CFG] = &slv_gpu_cfg,
	[SLAVE_BLSP_1] = &slv_blsp_1,
	[SLAVE_BLSP_2] = &slv_blsp_2,
	[SLAVE_TLMM_NORTH] = &slv_tlmm_north,
	[SLAVE_PCIE] = &slv_pcie,
	[SLAVE_ETHERNET] = &slv_ethernet,
	[SLAVE_TLMM_EAST] = &slv_tlmm_east,
	[SLAVE_TCU] = &slv_tcu,
	[SLAVE_PMIC_ARB] = &slv_pmic_arb,
	[SLAVE_SDCC_1] = &slv_sdcc_1,
	[SLAVE_SDCC_2] = &slv_sdcc_2,
	[SLAVE_TLMM_SOUTH] = &slv_tlmm_south,
	[SLAVE_USB_HS] = &slv_usb_hs,
	[SLAVE_USB3] = &slv_usb3,
	[SLAVE_CRYPTO_0_CFG] = &slv_crypto_0_cfg,
	[SLAVE_PCNOC_SNOC] = &slv_pcnoc_snoc,
};

static struct qcom_icc_desc qcs404_pcnoc = {
	.nodes = qcs404_pcnoc_nodes,
	.num_nodes = ARRAY_SIZE(qcs404_pcnoc_nodes),
};

static struct qcom_icc_node *qcs404_snoc_nodes[] = {
	[MASTER_QDSS_BAM] = &mas_qdss_bam,
	[MASTER_BIMC_SNOC] = &mas_bimc_snoc,
	[MASTER_PCNOC_SNOC] = &mas_pcnoc_snoc,
	[MASTER_QDSS_ETR] = &mas_qdss_etr,
	[MASTER_EMAC] = &mas_emac,
	[MASTER_PCIE] = &mas_pcie,
	[MASTER_USB3] = &mas_usb3,
	[QDSS_INT] = &qdss_int,
	[SNOC_INT_0] = &snoc_int_0,
	[SNOC_INT_1] = &snoc_int_1,
	[SNOC_INT_2] = &snoc_int_2,
	[SLAVE_KPSS_AHB] = &slv_kpss_ahb,
	[SLAVE_WCSS] = &slv_wcss,
	[SLAVE_SNOC_BIMC_1] = &slv_snoc_bimc_1,
	[SLAVE_IMEM] = &slv_imem,
	[SLAVE_SNOC_PCNOC] = &slv_snoc_pcnoc,
	[SLAVE_QDSS_STM] = &slv_qdss_stm,
	[SLAVE_CATS_0] = &slv_cats_0,
	[SLAVE_CATS_1] = &slv_cats_1,
	[SLAVE_LPASS] = &slv_lpass,
};

static struct qcom_icc_desc qcs404_snoc = {
	.nodes = qcs404_snoc_nodes,
	.num_nodes = ARRAY_SIZE(qcs404_snoc_nodes),
};


static const struct of_device_id qcs404_noc_of_match[] = {
	{ .compatible = "qcom,qcs404-bimc", .data = &qcs404_bimc },
	{ .compatible = "qcom,qcs404-pcnoc", .data = &qcs404_pcnoc },
	{ .compatible = "qcom,qcs404-snoc", .data = &qcs404_snoc },
	{ },
};
MODULE_DEVICE_TABLE(of, qcs404_noc_of_match);

static struct platform_driver qcs404_noc_driver = {
	.probe = qnoc_probe,
	.remove = qnoc_remove,
	.driver = {
		.name = "qnoc-qcs404",
		.of_match_table = qcs404_noc_of_match,
	},
};
module_platform_driver(qcs404_noc_driver);
MODULE_DESCRIPTION("Qualcomm QCS404 NoC driver");
MODULE_LICENSE("GPL v2");
