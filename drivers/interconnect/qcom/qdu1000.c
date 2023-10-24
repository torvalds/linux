// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <dt-bindings/interconnect/qcom,qdu1000-rpmh.h>

#include "bcm-voter.h"
#include "icc-common.h"
#include "icc-rpmh.h"
#include "qdu1000.h"

static struct qcom_icc_node qup0_core_master = {
	.name = "qup0_core_master",
	.id = QDU1000_MASTER_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_SLAVE_QUP_CORE_0 },
};

static struct qcom_icc_node qup1_core_master = {
	.name = "qup1_core_master",
	.id = QDU1000_MASTER_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_SLAVE_QUP_CORE_1 },
};

static struct qcom_icc_node alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.id = QDU1000_MASTER_SYS_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { QDU1000_SLAVE_GEM_NOC_CNOC, QDU1000_SLAVE_LLCC },
};

static struct qcom_icc_node chm_apps = {
	.name = "chm_apps",
	.id = QDU1000_MASTER_APPSS_PROC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 4,
	.links = { QDU1000_SLAVE_GEM_NOC_CNOC, QDU1000_SLAVE_LLCC,
		   QDU1000_SLAVE_GEMNOC_MODEM_CNOC, QDU1000_SLAVE_MEM_NOC_PCIE_SNOC
	},
};

static struct qcom_icc_node qnm_ecpri_dma = {
	.name = "qnm_ecpri_dma",
	.id = QDU1000_MASTER_GEMNOC_ECPRI_DMA,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { QDU1000_SLAVE_GEM_NOC_CNOC, QDU1000_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_fec_2_gemnoc = {
	.name = "qnm_fec_2_gemnoc",
	.id = QDU1000_MASTER_FEC_2_GEMNOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { QDU1000_SLAVE_GEM_NOC_CNOC, QDU1000_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_pcie = {
	.name = "qnm_pcie",
	.id = QDU1000_MASTER_ANOC_PCIE_GEM_NOC,
	.channels = 1,
	.buswidth = 64,
	.num_links = 3,
	.links = { QDU1000_SLAVE_GEM_NOC_CNOC, QDU1000_SLAVE_LLCC,
		   QDU1000_SLAVE_GEMNOC_MODEM_CNOC
	},
};

static struct qcom_icc_node qnm_snoc_gc = {
	.name = "qnm_snoc_gc",
	.id = QDU1000_MASTER_SNOC_GC_MEM_NOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_snoc_sf = {
	.name = "qnm_snoc_sf",
	.id = QDU1000_MASTER_SNOC_SF_MEM_NOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 4,
	.links = { QDU1000_SLAVE_GEM_NOC_CNOC, QDU1000_SLAVE_LLCC,
		   QDU1000_SLAVE_GEMNOC_MODEM_CNOC, QDU1000_SLAVE_MEM_NOC_PCIE_SNOC
	},
};

static struct qcom_icc_node qxm_mdsp = {
	.name = "qxm_mdsp",
	.id = QDU1000_MASTER_MSS_PROC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { QDU1000_SLAVE_GEM_NOC_CNOC, QDU1000_SLAVE_LLCC,
		   QDU1000_SLAVE_MEM_NOC_PCIE_SNOC
	},
};

static struct qcom_icc_node llcc_mc = {
	.name = "llcc_mc",
	.id = QDU1000_MASTER_LLCC,
	.channels = 8,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_SLAVE_EBI1 },
};

static struct qcom_icc_node qhm_gic = {
	.name = "qhm_gic",
	.id = QDU1000_MASTER_GIC_AHB,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = QDU1000_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node qhm_qpic = {
	.name = "qhm_qpic",
	.id = QDU1000_MASTER_QPIC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node qhm_qspi = {
	.name = "qhm_qspi",
	.id = QDU1000_MASTER_QSPI_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node qhm_qup0 = {
	.name = "qhm_qup0",
	.id = QDU1000_MASTER_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node qhm_qup1 = {
	.name = "qhm_qup1",
	.id = QDU1000_MASTER_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node qhm_system_noc_cfg = {
	.name = "qhm_system_noc_cfg",
	.id = QDU1000_MASTER_SNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_SLAVE_SERVICE_SNOC },
};

static struct qcom_icc_node qnm_aggre_noc = {
	.name = "qnm_aggre_noc",
	.id = QDU1000_MASTER_ANOC_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node qnm_aggre_noc_gsi = {
	.name = "qnm_aggre_noc_gsi",
	.id = QDU1000_MASTER_ANOC_GSI,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_SLAVE_SNOC_GEM_NOC_GC },
};

static struct qcom_icc_node qnm_gemnoc_cnoc = {
	.name = "qnm_gemnoc_cnoc",
	.id = QDU1000_MASTER_GEM_NOC_CNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 36,
	.links = { QDU1000_SLAVE_AHB2PHY_SOUTH, QDU1000_SLAVE_AHB2PHY_NORTH,
		   QDU1000_SLAVE_AHB2PHY_EAST, QDU1000_SLAVE_AOSS,
		   QDU1000_SLAVE_CLK_CTL, QDU1000_SLAVE_RBCPR_CX_CFG,
		   QDU1000_SLAVE_RBCPR_MX_CFG, QDU1000_SLAVE_CRYPTO_0_CFG,
		   QDU1000_SLAVE_ECPRI_CFG, QDU1000_SLAVE_IMEM_CFG,
		   QDU1000_SLAVE_IPC_ROUTER_CFG, QDU1000_SLAVE_CNOC_MSS,
		   QDU1000_SLAVE_PCIE_CFG, QDU1000_SLAVE_PDM,
		   QDU1000_SLAVE_PIMEM_CFG, QDU1000_SLAVE_PRNG,
		   QDU1000_SLAVE_QDSS_CFG, QDU1000_SLAVE_QPIC,
		   QDU1000_SLAVE_QSPI_0, QDU1000_SLAVE_QUP_0,
		   QDU1000_SLAVE_QUP_1, QDU1000_SLAVE_SDCC_2,
		   QDU1000_SLAVE_SMBUS_CFG, QDU1000_SLAVE_SNOC_CFG,
		   QDU1000_SLAVE_TCSR, QDU1000_SLAVE_TLMM,
		   QDU1000_SLAVE_TME_CFG, QDU1000_SLAVE_TSC_CFG,
		   QDU1000_SLAVE_USB3_0, QDU1000_SLAVE_VSENSE_CTRL_CFG,
		   QDU1000_SLAVE_DDRSS_CFG, QDU1000_SLAVE_IMEM,
		   QDU1000_SLAVE_PIMEM, QDU1000_SLAVE_ETHERNET_SS,
		   QDU1000_SLAVE_QDSS_STM, QDU1000_SLAVE_TCU
	},
};

static struct qcom_icc_node qnm_gemnoc_modem_slave = {
	.name = "qnm_gemnoc_modem_slave",
	.id = QDU1000_MASTER_GEMNOC_MODEM_CNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { QDU1000_SLAVE_MODEM_OFFLINE },
};

static struct qcom_icc_node qnm_gemnoc_pcie = {
	.name = "qnm_gemnoc_pcie",
	.id = QDU1000_MASTER_GEM_NOC_PCIE_SNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { QDU1000_SLAVE_PCIE_0 },
};

static struct qcom_icc_node qxm_crypto = {
	.name = "qxm_crypto",
	.id = QDU1000_MASTER_CRYPTO,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node qxm_ecpri_gsi = {
	.name = "qxm_ecpri_gsi",
	.id = QDU1000_MASTER_ECPRI_GSI,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { QDU1000_SLAVE_ANOC_SNOC_GSI, QDU1000_SLAVE_PCIE_0 },
};

static struct qcom_icc_node qxm_pimem = {
	.name = "qxm_pimem",
	.id = QDU1000_MASTER_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_SLAVE_SNOC_GEM_NOC_GC },
};

static struct qcom_icc_node xm_ecpri_dma = {
	.name = "xm_ecpri_dma",
	.id = QDU1000_MASTER_SNOC_ECPRI_DMA,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { QDU1000_SLAVE_ECPRI_GEMNOC, QDU1000_SLAVE_PCIE_0 },
};

static struct qcom_icc_node xm_gic = {
	.name = "xm_gic",
	.id = QDU1000_MASTER_GIC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_SLAVE_SNOC_GEM_NOC_GC },
};

static struct qcom_icc_node xm_pcie = {
	.name = "xm_pcie",
	.id = QDU1000_MASTER_PCIE,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { QDU1000_SLAVE_ANOC_PCIE_GEM_NOC },
};

static struct qcom_icc_node xm_qdss_etr0 = {
	.name = "xm_qdss_etr0",
	.id = QDU1000_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node xm_qdss_etr1 = {
	.name = "xm_qdss_etr1",
	.id = QDU1000_MASTER_QDSS_ETR_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node xm_sdc = {
	.name = "xm_sdc",
	.id = QDU1000_MASTER_SDCC_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node xm_usb3 = {
	.name = "xm_usb3",
	.id = QDU1000_MASTER_USB3,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node qup0_core_slave = {
	.name = "qup0_core_slave",
	.id = QDU1000_SLAVE_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qup1_core_slave = {
	.name = "qup1_core_slave",
	.id = QDU1000_SLAVE_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qns_gem_noc_cnoc = {
	.name = "qns_gem_noc_cnoc",
	.id = QDU1000_SLAVE_GEM_NOC_CNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { QDU1000_MASTER_GEM_NOC_CNOC },
};

static struct qcom_icc_node qns_llcc = {
	.name = "qns_llcc",
	.id = QDU1000_SLAVE_LLCC,
	.channels = 8,
	.buswidth = 16,
	.num_links = 1,
	.links = { QDU1000_MASTER_LLCC },
};

static struct qcom_icc_node qns_modem_slave = {
	.name = "qns_modem_slave",
	.id = QDU1000_SLAVE_GEMNOC_MODEM_CNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { QDU1000_MASTER_GEMNOC_MODEM_CNOC },
};

static struct qcom_icc_node qns_pcie = {
	.name = "qns_pcie",
	.id = QDU1000_SLAVE_MEM_NOC_PCIE_SNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { QDU1000_MASTER_GEM_NOC_PCIE_SNOC },
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.id = QDU1000_SLAVE_EBI1,
	.channels = 8,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ahb2phy0_south = {
	.name = "qhs_ahb2phy0_south",
	.id = QDU1000_SLAVE_AHB2PHY_SOUTH,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ahb2phy1_north = {
	.name = "qhs_ahb2phy1_north",
	.id = QDU1000_SLAVE_AHB2PHY_NORTH,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ahb2phy2_east = {
	.name = "qhs_ahb2phy2_east",
	.id = QDU1000_SLAVE_AHB2PHY_EAST,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_aoss = {
	.name = "qhs_aoss",
	.id = QDU1000_SLAVE_AOSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = QDU1000_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = QDU1000_SLAVE_RBCPR_CX_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_cpr_mx = {
	.name = "qhs_cpr_mx",
	.id = QDU1000_SLAVE_RBCPR_MX_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_crypto_cfg = {
	.name = "qhs_crypto_cfg",
	.id = QDU1000_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ecpri_cfg = {
	.name = "qhs_ecpri_cfg",
	.id = QDU1000_SLAVE_ECPRI_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = QDU1000_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ipc_router = {
	.name = "qhs_ipc_router",
	.id = QDU1000_SLAVE_IPC_ROUTER_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_mss_cfg = {
	.name = "qhs_mss_cfg",
	.id = QDU1000_SLAVE_CNOC_MSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pcie_cfg = {
	.name = "qhs_pcie_cfg",
	.id = QDU1000_SLAVE_PCIE_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pdm = {
	.name = "qhs_pdm",
	.id = QDU1000_SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = QDU1000_SLAVE_PIMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_prng = {
	.name = "qhs_prng",
	.id = QDU1000_SLAVE_PRNG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = QDU1000_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qpic = {
	.name = "qhs_qpic",
	.id = QDU1000_SLAVE_QPIC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qspi = {
	.name = "qhs_qspi",
	.id = QDU1000_SLAVE_QSPI_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qup0 = {
	.name = "qhs_qup0",
	.id = QDU1000_SLAVE_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qup1 = {
	.name = "qhs_qup1",
	.id = QDU1000_SLAVE_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = QDU1000_SLAVE_SDCC_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_smbus_cfg = {
	.name = "qhs_smbus_cfg",
	.id = QDU1000_SLAVE_SMBUS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_system_noc_cfg = {
	.name = "qhs_system_noc_cfg",
	.id = QDU1000_SLAVE_SNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_MASTER_SNOC_CFG },
};

static struct qcom_icc_node qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = QDU1000_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_tlmm = {
	.name = "qhs_tlmm",
	.id = QDU1000_SLAVE_TLMM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_tme_cfg = {
	.name = "qhs_tme_cfg",
	.id = QDU1000_SLAVE_TME_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_tsc_cfg = {
	.name = "qhs_tsc_cfg",
	.id = QDU1000_SLAVE_TSC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_usb3 = {
	.name = "qhs_usb3",
	.id = QDU1000_SLAVE_USB3_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.id = QDU1000_SLAVE_VSENSE_CTRL_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qns_a1noc_snoc = {
	.name = "qns_a1noc_snoc",
	.id = QDU1000_SLAVE_A1NOC_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_MASTER_ANOC_SNOC },
};

static struct qcom_icc_node qns_anoc_snoc_gsi = {
	.name = "qns_anoc_snoc_gsi",
	.id = QDU1000_SLAVE_ANOC_SNOC_GSI,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_MASTER_ANOC_GSI },
};

static struct qcom_icc_node qns_ddrss_cfg = {
	.name = "qns_ddrss_cfg",
	.id = QDU1000_SLAVE_DDRSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qns_ecpri_gemnoc = {
	.name = "qns_ecpri_gemnoc",
	.id = QDU1000_SLAVE_ECPRI_GEMNOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { QDU1000_MASTER_GEMNOC_ECPRI_DMA },
};

static struct qcom_icc_node qns_gemnoc_gc = {
	.name = "qns_gemnoc_gc",
	.id = QDU1000_SLAVE_SNOC_GEM_NOC_GC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_MASTER_SNOC_GC_MEM_NOC },
};

static struct qcom_icc_node qns_gemnoc_sf = {
	.name = "qns_gemnoc_sf",
	.id = QDU1000_SLAVE_SNOC_GEM_NOC_SF,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { QDU1000_MASTER_SNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qns_modem = {
	.name = "qns_modem",
	.id = QDU1000_SLAVE_MODEM_OFFLINE,
	.channels = 1,
	.buswidth = 32,
	.num_links = 0,
};

static struct qcom_icc_node qns_pcie_gemnoc = {
	.name = "qns_pcie_gemnoc",
	.id = QDU1000_SLAVE_ANOC_PCIE_GEM_NOC,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { QDU1000_MASTER_ANOC_PCIE_GEM_NOC },
};

static struct qcom_icc_node qxs_imem = {
	.name = "qxs_imem",
	.id = QDU1000_SLAVE_IMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node qxs_pimem = {
	.name = "qxs_pimem",
	.id = QDU1000_SLAVE_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node srvc_system_noc = {
	.name = "srvc_system_noc",
	.id = QDU1000_SLAVE_SERVICE_SNOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node xs_ethernet_ss = {
	.name = "xs_ethernet_ss",
	.id = QDU1000_SLAVE_ETHERNET_SS,
	.channels = 1,
	.buswidth = 32,
	.num_links = 0,
};

static struct qcom_icc_node xs_pcie = {
	.name = "xs_pcie",
	.id = QDU1000_SLAVE_PCIE_0,
	.channels = 1,
	.buswidth = 64,
	.num_links = 0,
};

static struct qcom_icc_node xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = QDU1000_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = QDU1000_SLAVE_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.num_nodes = 1,
	.nodes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.num_nodes = 44,
	.nodes = { &qhm_qpic, &qhm_qspi,
		   &qnm_gemnoc_cnoc, &qnm_gemnoc_modem_slave,
		   &qnm_gemnoc_pcie, &xm_sdc,
		   &xm_usb3, &qhs_ahb2phy0_south,
		   &qhs_ahb2phy1_north, &qhs_ahb2phy2_east,
		   &qhs_aoss, &qhs_clk_ctl,
		   &qhs_cpr_cx, &qhs_cpr_mx,
		   &qhs_crypto_cfg, &qhs_ecpri_cfg,
		   &qhs_imem_cfg, &qhs_ipc_router,
		   &qhs_mss_cfg, &qhs_pcie_cfg,
		   &qhs_pdm, &qhs_pimem_cfg,
		   &qhs_prng, &qhs_qdss_cfg,
		   &qhs_qpic, &qhs_qspi,
		   &qhs_qup0, &qhs_qup1,
		   &qhs_sdc2, &qhs_smbus_cfg,
		   &qhs_system_noc_cfg, &qhs_tcsr,
		   &qhs_tlmm, &qhs_tme_cfg,
		   &qhs_tsc_cfg, &qhs_usb3,
		   &qhs_vsense_ctrl_cfg, &qns_ddrss_cfg,
		   &qns_modem, &qxs_imem,
		   &qxs_pimem, &xs_ethernet_ss,
		   &xs_qdss_stm, &xs_sys_tcu_cfg
	},
};

static struct qcom_icc_bcm bcm_mc0 = {
	.name = "MC0",
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.num_nodes = 2,
	.nodes = { &qup0_core_slave, &qup1_core_slave },
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.num_nodes = 1,
	.nodes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_sh1 = {
	.name = "SH1",
	.num_nodes = 11,
	.nodes = { &alm_sys_tcu, &chm_apps,
		   &qnm_ecpri_dma, &qnm_fec_2_gemnoc,
		   &qnm_pcie, &qnm_snoc_gc,
		   &qnm_snoc_sf, &qxm_mdsp,
		   &qns_gem_noc_cnoc, &qns_modem_slave,
		   &qns_pcie
	},
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.num_nodes = 1,
	.nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.num_nodes = 6,
	.nodes = { &qhm_gic, &qxm_pimem,
		   &xm_gic, &xm_qdss_etr0,
		   &xm_qdss_etr1, &qns_gemnoc_gc
	},
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.num_nodes = 5,
	.nodes = { &qnm_aggre_noc, &qxm_ecpri_gsi,
		   &xm_ecpri_dma, &qns_anoc_snoc_gsi,
		   &qns_ecpri_gemnoc
	},
};

static struct qcom_icc_bcm bcm_sn7 = {
	.name = "SN7",
	.num_nodes = 2,
	.nodes = { &qns_pcie_gemnoc, &xs_pcie },
};

static struct qcom_icc_bcm * const clk_virt_bcms[] = {
	&bcm_qup0,
};

static struct qcom_icc_node * const clk_virt_nodes[] = {
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[MASTER_QUP_CORE_1] = &qup1_core_master,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[SLAVE_QUP_CORE_1] = &qup1_core_slave,
};

static const struct qcom_icc_desc qdu1000_clk_virt = {
	.nodes = clk_virt_nodes,
	.num_nodes = ARRAY_SIZE(clk_virt_nodes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
};

static struct qcom_icc_bcm * const gem_noc_bcms[] = {
	&bcm_sh0,
	&bcm_sh1,
};

static struct qcom_icc_node * const gem_noc_nodes[] = {
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_APPSS_PROC] = &chm_apps,
	[MASTER_GEMNOC_ECPRI_DMA] = &qnm_ecpri_dma,
	[MASTER_FEC_2_GEMNOC] = &qnm_fec_2_gemnoc,
	[MASTER_ANOC_PCIE_GEM_NOC] = &qnm_pcie,
	[MASTER_SNOC_GC_MEM_NOC] = &qnm_snoc_gc,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[MASTER_MSS_PROC] = &qxm_mdsp,
	[SLAVE_GEM_NOC_CNOC] = &qns_gem_noc_cnoc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_GEMNOC_MODEM_CNOC] = &qns_modem_slave,
	[SLAVE_MEM_NOC_PCIE_SNOC] = &qns_pcie,
};

static const struct qcom_icc_desc qdu1000_gem_noc = {
	.nodes = gem_noc_nodes,
	.num_nodes = ARRAY_SIZE(gem_noc_nodes),
	.bcms = gem_noc_bcms,
	.num_bcms = ARRAY_SIZE(gem_noc_bcms),
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
};

static struct qcom_icc_node * const mc_virt_nodes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
};

static const struct qcom_icc_desc qdu1000_mc_virt = {
	.nodes = mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm * const system_noc_bcms[] = {
	&bcm_ce0,
	&bcm_cn0,
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn2,
	&bcm_sn7,
};

static struct qcom_icc_node * const system_noc_nodes[] = {
	[MASTER_GIC_AHB] = &qhm_gic,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QPIC] = &qhm_qpic,
	[MASTER_QSPI_0] = &qhm_qspi,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_QUP_1] = &qhm_qup1,
	[MASTER_SNOC_CFG] = &qhm_system_noc_cfg,
	[MASTER_ANOC_SNOC] = &qnm_aggre_noc,
	[MASTER_ANOC_GSI] = &qnm_aggre_noc_gsi,
	[MASTER_GEM_NOC_CNOC] = &qnm_gemnoc_cnoc,
	[MASTER_GEMNOC_MODEM_CNOC] = &qnm_gemnoc_modem_slave,
	[MASTER_GEM_NOC_PCIE_SNOC] = &qnm_gemnoc_pcie,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_ECPRI_GSI] = &qxm_ecpri_gsi,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_SNOC_ECPRI_DMA] = &xm_ecpri_dma,
	[MASTER_GIC] = &xm_gic,
	[MASTER_PCIE] = &xm_pcie,
	[MASTER_QDSS_ETR] = &xm_qdss_etr0,
	[MASTER_QDSS_ETR_1] = &xm_qdss_etr1,
	[MASTER_SDCC_1] = &xm_sdc,
	[MASTER_USB3] = &xm_usb3,
	[SLAVE_AHB2PHY_SOUTH] = &qhs_ahb2phy0_south,
	[SLAVE_AHB2PHY_NORTH] = &qhs_ahb2phy1_north,
	[SLAVE_AHB2PHY_EAST] = &qhs_ahb2phy2_east,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MX_CFG] = &qhs_cpr_mx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto_cfg,
	[SLAVE_ECPRI_CFG] = &qhs_ecpri_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPC_ROUTER_CFG] = &qhs_ipc_router,
	[SLAVE_CNOC_MSS] = &qhs_mss_cfg,
	[SLAVE_PCIE_CFG] = &qhs_pcie_cfg,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QPIC] = &qhs_qpic,
	[SLAVE_QSPI_0] = &qhs_qspi,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SMBUS_CFG] = &qhs_smbus_cfg,
	[SLAVE_SNOC_CFG] = &qhs_system_noc_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_TME_CFG] = &qhs_tme_cfg,
	[SLAVE_TSC_CFG] = &qhs_tsc_cfg,
	[SLAVE_USB3_0] = &qhs_usb3,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_A1NOC_SNOC] = &qns_a1noc_snoc,
	[SLAVE_ANOC_SNOC_GSI] = &qns_anoc_snoc_gsi,
	[SLAVE_DDRSS_CFG] = &qns_ddrss_cfg,
	[SLAVE_ECPRI_GEMNOC] = &qns_ecpri_gemnoc,
	[SLAVE_SNOC_GEM_NOC_GC] = &qns_gemnoc_gc,
	[SLAVE_SNOC_GEM_NOC_SF] = &qns_gemnoc_sf,
	[SLAVE_MODEM_OFFLINE] = &qns_modem,
	[SLAVE_ANOC_PCIE_GEM_NOC] = &qns_pcie_gemnoc,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SERVICE_SNOC] = &srvc_system_noc,
	[SLAVE_ETHERNET_SS] = &xs_ethernet_ss,
	[SLAVE_PCIE_0] = &xs_pcie,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc qdu1000_system_noc = {
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static int qnoc_probe(struct platform_device *pdev)
{
	int ret;

	ret = qcom_icc_rpmh_probe(pdev);
	if (ret)
		dev_err(&pdev->dev, "failed to register ICC provider\n");

	return ret;
}

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,qdu1000-clk-virt",
	  .data = &qdu1000_clk_virt
	},
	{ .compatible = "qcom,qdu1000-gem-noc",
	  .data = &qdu1000_gem_noc
	},
	{ .compatible = "qcom,qdu1000-mc-virt",
	  .data = &qdu1000_mc_virt
	},
	{ .compatible = "qcom,qdu1000-system-noc",
	  .data = &qdu1000_system_noc
	},
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qnoc_probe,
	.remove = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-qdu1000",
		.of_match_table = qnoc_of_match,
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

MODULE_DESCRIPTION("QDU1000 NoC driver");
MODULE_LICENSE("GPL");
