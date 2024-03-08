// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Qualcomm Inanalvation Center, Inc. All rights reserved.
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

static struct qcom_icc_analde qup0_core_master = {
	.name = "qup0_core_master",
	.id = QDU1000_MASTER_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_SLAVE_QUP_CORE_0 },
};

static struct qcom_icc_analde qup1_core_master = {
	.name = "qup1_core_master",
	.id = QDU1000_MASTER_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_SLAVE_QUP_CORE_1 },
};

static struct qcom_icc_analde alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.id = QDU1000_MASTER_SYS_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { QDU1000_SLAVE_GEM_ANALC_CANALC, QDU1000_SLAVE_LLCC },
};

static struct qcom_icc_analde chm_apps = {
	.name = "chm_apps",
	.id = QDU1000_MASTER_APPSS_PROC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 4,
	.links = { QDU1000_SLAVE_GEM_ANALC_CANALC, QDU1000_SLAVE_LLCC,
		   QDU1000_SLAVE_GEMANALC_MODEM_CANALC, QDU1000_SLAVE_MEM_ANALC_PCIE_SANALC
	},
};

static struct qcom_icc_analde qnm_ecpri_dma = {
	.name = "qnm_ecpri_dma",
	.id = QDU1000_MASTER_GEMANALC_ECPRI_DMA,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { QDU1000_SLAVE_GEM_ANALC_CANALC, QDU1000_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_fec_2_gemanalc = {
	.name = "qnm_fec_2_gemanalc",
	.id = QDU1000_MASTER_FEC_2_GEMANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { QDU1000_SLAVE_GEM_ANALC_CANALC, QDU1000_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_pcie = {
	.name = "qnm_pcie",
	.id = QDU1000_MASTER_AANALC_PCIE_GEM_ANALC,
	.channels = 1,
	.buswidth = 64,
	.num_links = 3,
	.links = { QDU1000_SLAVE_GEM_ANALC_CANALC, QDU1000_SLAVE_LLCC,
		   QDU1000_SLAVE_GEMANALC_MODEM_CANALC
	},
};

static struct qcom_icc_analde qnm_sanalc_gc = {
	.name = "qnm_sanalc_gc",
	.id = QDU1000_MASTER_SANALC_GC_MEM_ANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_sanalc_sf = {
	.name = "qnm_sanalc_sf",
	.id = QDU1000_MASTER_SANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 4,
	.links = { QDU1000_SLAVE_GEM_ANALC_CANALC, QDU1000_SLAVE_LLCC,
		   QDU1000_SLAVE_GEMANALC_MODEM_CANALC, QDU1000_SLAVE_MEM_ANALC_PCIE_SANALC
	},
};

static struct qcom_icc_analde qxm_mdsp = {
	.name = "qxm_mdsp",
	.id = QDU1000_MASTER_MSS_PROC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { QDU1000_SLAVE_GEM_ANALC_CANALC, QDU1000_SLAVE_LLCC,
		   QDU1000_SLAVE_MEM_ANALC_PCIE_SANALC
	},
};

static struct qcom_icc_analde llcc_mc = {
	.name = "llcc_mc",
	.id = QDU1000_MASTER_LLCC,
	.channels = 8,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_SLAVE_EBI1 },
};

static struct qcom_icc_analde qhm_gic = {
	.name = "qhm_gic",
	.id = QDU1000_MASTER_GIC_AHB,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = QDU1000_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qhm_qpic = {
	.name = "qhm_qpic",
	.id = QDU1000_MASTER_QPIC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qspi = {
	.name = "qhm_qspi",
	.id = QDU1000_MASTER_QSPI_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qup0 = {
	.name = "qhm_qup0",
	.id = QDU1000_MASTER_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qhm_qup1 = {
	.name = "qhm_qup1",
	.id = QDU1000_MASTER_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qhm_system_analc_cfg = {
	.name = "qhm_system_analc_cfg",
	.id = QDU1000_MASTER_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_SLAVE_SERVICE_SANALC },
};

static struct qcom_icc_analde qnm_aggre_analc = {
	.name = "qnm_aggre_analc",
	.id = QDU1000_MASTER_AANALC_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qnm_aggre_analc_gsi = {
	.name = "qnm_aggre_analc_gsi",
	.id = QDU1000_MASTER_AANALC_GSI,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_SLAVE_SANALC_GEM_ANALC_GC },
};

static struct qcom_icc_analde qnm_gemanalc_canalc = {
	.name = "qnm_gemanalc_canalc",
	.id = QDU1000_MASTER_GEM_ANALC_CANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 36,
	.links = { QDU1000_SLAVE_AHB2PHY_SOUTH, QDU1000_SLAVE_AHB2PHY_ANALRTH,
		   QDU1000_SLAVE_AHB2PHY_EAST, QDU1000_SLAVE_AOSS,
		   QDU1000_SLAVE_CLK_CTL, QDU1000_SLAVE_RBCPR_CX_CFG,
		   QDU1000_SLAVE_RBCPR_MX_CFG, QDU1000_SLAVE_CRYPTO_0_CFG,
		   QDU1000_SLAVE_ECPRI_CFG, QDU1000_SLAVE_IMEM_CFG,
		   QDU1000_SLAVE_IPC_ROUTER_CFG, QDU1000_SLAVE_CANALC_MSS,
		   QDU1000_SLAVE_PCIE_CFG, QDU1000_SLAVE_PDM,
		   QDU1000_SLAVE_PIMEM_CFG, QDU1000_SLAVE_PRNG,
		   QDU1000_SLAVE_QDSS_CFG, QDU1000_SLAVE_QPIC,
		   QDU1000_SLAVE_QSPI_0, QDU1000_SLAVE_QUP_0,
		   QDU1000_SLAVE_QUP_1, QDU1000_SLAVE_SDCC_2,
		   QDU1000_SLAVE_SMBUS_CFG, QDU1000_SLAVE_SANALC_CFG,
		   QDU1000_SLAVE_TCSR, QDU1000_SLAVE_TLMM,
		   QDU1000_SLAVE_TME_CFG, QDU1000_SLAVE_TSC_CFG,
		   QDU1000_SLAVE_USB3_0, QDU1000_SLAVE_VSENSE_CTRL_CFG,
		   QDU1000_SLAVE_DDRSS_CFG, QDU1000_SLAVE_IMEM,
		   QDU1000_SLAVE_PIMEM, QDU1000_SLAVE_ETHERNET_SS,
		   QDU1000_SLAVE_QDSS_STM, QDU1000_SLAVE_TCU
	},
};

static struct qcom_icc_analde qnm_gemanalc_modem_slave = {
	.name = "qnm_gemanalc_modem_slave",
	.id = QDU1000_MASTER_GEMANALC_MODEM_CANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { QDU1000_SLAVE_MODEM_OFFLINE },
};

static struct qcom_icc_analde qnm_gemanalc_pcie = {
	.name = "qnm_gemanalc_pcie",
	.id = QDU1000_MASTER_GEM_ANALC_PCIE_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { QDU1000_SLAVE_PCIE_0 },
};

static struct qcom_icc_analde qxm_crypto = {
	.name = "qxm_crypto",
	.id = QDU1000_MASTER_CRYPTO,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qxm_ecpri_gsi = {
	.name = "qxm_ecpri_gsi",
	.id = QDU1000_MASTER_ECPRI_GSI,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { QDU1000_SLAVE_AANALC_SANALC_GSI, QDU1000_SLAVE_PCIE_0 },
};

static struct qcom_icc_analde qxm_pimem = {
	.name = "qxm_pimem",
	.id = QDU1000_MASTER_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_SLAVE_SANALC_GEM_ANALC_GC },
};

static struct qcom_icc_analde xm_ecpri_dma = {
	.name = "xm_ecpri_dma",
	.id = QDU1000_MASTER_SANALC_ECPRI_DMA,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { QDU1000_SLAVE_ECPRI_GEMANALC, QDU1000_SLAVE_PCIE_0 },
};

static struct qcom_icc_analde xm_gic = {
	.name = "xm_gic",
	.id = QDU1000_MASTER_GIC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_SLAVE_SANALC_GEM_ANALC_GC },
};

static struct qcom_icc_analde xm_pcie = {
	.name = "xm_pcie",
	.id = QDU1000_MASTER_PCIE,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { QDU1000_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde xm_qdss_etr0 = {
	.name = "xm_qdss_etr0",
	.id = QDU1000_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde xm_qdss_etr1 = {
	.name = "xm_qdss_etr1",
	.id = QDU1000_MASTER_QDSS_ETR_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde xm_sdc = {
	.name = "xm_sdc",
	.id = QDU1000_MASTER_SDCC_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde xm_usb3 = {
	.name = "xm_usb3",
	.id = QDU1000_MASTER_USB3,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_SLAVE_A1ANALC_SANALC },
};

static struct qcom_icc_analde qup0_core_slave = {
	.name = "qup0_core_slave",
	.id = QDU1000_SLAVE_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qup1_core_slave = {
	.name = "qup1_core_slave",
	.id = QDU1000_SLAVE_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_gem_analc_canalc = {
	.name = "qns_gem_analc_canalc",
	.id = QDU1000_SLAVE_GEM_ANALC_CANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { QDU1000_MASTER_GEM_ANALC_CANALC },
};

static struct qcom_icc_analde qns_llcc = {
	.name = "qns_llcc",
	.id = QDU1000_SLAVE_LLCC,
	.channels = 8,
	.buswidth = 16,
	.num_links = 1,
	.links = { QDU1000_MASTER_LLCC },
};

static struct qcom_icc_analde qns_modem_slave = {
	.name = "qns_modem_slave",
	.id = QDU1000_SLAVE_GEMANALC_MODEM_CANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { QDU1000_MASTER_GEMANALC_MODEM_CANALC },
};

static struct qcom_icc_analde qns_pcie = {
	.name = "qns_pcie",
	.id = QDU1000_SLAVE_MEM_ANALC_PCIE_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { QDU1000_MASTER_GEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde ebi = {
	.name = "ebi",
	.id = QDU1000_SLAVE_EBI1,
	.channels = 8,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ahb2phy0_south = {
	.name = "qhs_ahb2phy0_south",
	.id = QDU1000_SLAVE_AHB2PHY_SOUTH,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ahb2phy1_analrth = {
	.name = "qhs_ahb2phy1_analrth",
	.id = QDU1000_SLAVE_AHB2PHY_ANALRTH,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ahb2phy2_east = {
	.name = "qhs_ahb2phy2_east",
	.id = QDU1000_SLAVE_AHB2PHY_EAST,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_aoss = {
	.name = "qhs_aoss",
	.id = QDU1000_SLAVE_AOSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = QDU1000_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = QDU1000_SLAVE_RBCPR_CX_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_cpr_mx = {
	.name = "qhs_cpr_mx",
	.id = QDU1000_SLAVE_RBCPR_MX_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_crypto_cfg = {
	.name = "qhs_crypto_cfg",
	.id = QDU1000_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ecpri_cfg = {
	.name = "qhs_ecpri_cfg",
	.id = QDU1000_SLAVE_ECPRI_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = QDU1000_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ipc_router = {
	.name = "qhs_ipc_router",
	.id = QDU1000_SLAVE_IPC_ROUTER_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_mss_cfg = {
	.name = "qhs_mss_cfg",
	.id = QDU1000_SLAVE_CANALC_MSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pcie_cfg = {
	.name = "qhs_pcie_cfg",
	.id = QDU1000_SLAVE_PCIE_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pdm = {
	.name = "qhs_pdm",
	.id = QDU1000_SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = QDU1000_SLAVE_PIMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_prng = {
	.name = "qhs_prng",
	.id = QDU1000_SLAVE_PRNG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = QDU1000_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qpic = {
	.name = "qhs_qpic",
	.id = QDU1000_SLAVE_QPIC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qspi = {
	.name = "qhs_qspi",
	.id = QDU1000_SLAVE_QSPI_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qup0 = {
	.name = "qhs_qup0",
	.id = QDU1000_SLAVE_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qup1 = {
	.name = "qhs_qup1",
	.id = QDU1000_SLAVE_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = QDU1000_SLAVE_SDCC_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_smbus_cfg = {
	.name = "qhs_smbus_cfg",
	.id = QDU1000_SLAVE_SMBUS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_system_analc_cfg = {
	.name = "qhs_system_analc_cfg",
	.id = QDU1000_SLAVE_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { QDU1000_MASTER_SANALC_CFG },
};

static struct qcom_icc_analde qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = QDU1000_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_tlmm = {
	.name = "qhs_tlmm",
	.id = QDU1000_SLAVE_TLMM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_tme_cfg = {
	.name = "qhs_tme_cfg",
	.id = QDU1000_SLAVE_TME_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_tsc_cfg = {
	.name = "qhs_tsc_cfg",
	.id = QDU1000_SLAVE_TSC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_usb3 = {
	.name = "qhs_usb3",
	.id = QDU1000_SLAVE_USB3_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.id = QDU1000_SLAVE_VSENSE_CTRL_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_a1analc_sanalc = {
	.name = "qns_a1analc_sanalc",
	.id = QDU1000_SLAVE_A1ANALC_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_MASTER_AANALC_SANALC },
};

static struct qcom_icc_analde qns_aanalc_sanalc_gsi = {
	.name = "qns_aanalc_sanalc_gsi",
	.id = QDU1000_SLAVE_AANALC_SANALC_GSI,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_MASTER_AANALC_GSI },
};

static struct qcom_icc_analde qns_ddrss_cfg = {
	.name = "qns_ddrss_cfg",
	.id = QDU1000_SLAVE_DDRSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_ecpri_gemanalc = {
	.name = "qns_ecpri_gemanalc",
	.id = QDU1000_SLAVE_ECPRI_GEMANALC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { QDU1000_MASTER_GEMANALC_ECPRI_DMA },
};

static struct qcom_icc_analde qns_gemanalc_gc = {
	.name = "qns_gemanalc_gc",
	.id = QDU1000_SLAVE_SANALC_GEM_ANALC_GC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { QDU1000_MASTER_SANALC_GC_MEM_ANALC },
};

static struct qcom_icc_analde qns_gemanalc_sf = {
	.name = "qns_gemanalc_sf",
	.id = QDU1000_SLAVE_SANALC_GEM_ANALC_SF,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { QDU1000_MASTER_SANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qns_modem = {
	.name = "qns_modem",
	.id = QDU1000_SLAVE_MODEM_OFFLINE,
	.channels = 1,
	.buswidth = 32,
	.num_links = 0,
};

static struct qcom_icc_analde qns_pcie_gemanalc = {
	.name = "qns_pcie_gemanalc",
	.id = QDU1000_SLAVE_AANALC_PCIE_GEM_ANALC,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { QDU1000_MASTER_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde qxs_imem = {
	.name = "qxs_imem",
	.id = QDU1000_SLAVE_IMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde qxs_pimem = {
	.name = "qxs_pimem",
	.id = QDU1000_SLAVE_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde srvc_system_analc = {
	.name = "srvc_system_analc",
	.id = QDU1000_SLAVE_SERVICE_SANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde xs_ethernet_ss = {
	.name = "xs_ethernet_ss",
	.id = QDU1000_SLAVE_ETHERNET_SS,
	.channels = 1,
	.buswidth = 32,
	.num_links = 0,
};

static struct qcom_icc_analde xs_pcie = {
	.name = "xs_pcie",
	.id = QDU1000_SLAVE_PCIE_0,
	.channels = 1,
	.buswidth = 64,
	.num_links = 0,
};

static struct qcom_icc_analde xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = QDU1000_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = QDU1000_SLAVE_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
	.enable_mask = BIT(3),
	.num_analdes = 1,
	.analdes = { &ebi },
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.num_analdes = 1,
	.analdes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.num_analdes = 44,
	.analdes = { &qhm_qpic, &qhm_qspi,
		   &qnm_gemanalc_canalc, &qnm_gemanalc_modem_slave,
		   &qnm_gemanalc_pcie, &xm_sdc,
		   &xm_usb3, &qhs_ahb2phy0_south,
		   &qhs_ahb2phy1_analrth, &qhs_ahb2phy2_east,
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
		   &qhs_system_analc_cfg, &qhs_tcsr,
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
	.num_analdes = 1,
	.analdes = { &ebi },
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.num_analdes = 2,
	.analdes = { &qup0_core_slave, &qup1_core_slave },
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.num_analdes = 1,
	.analdes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_sh1 = {
	.name = "SH1",
	.num_analdes = 11,
	.analdes = { &alm_sys_tcu, &chm_apps,
		   &qnm_ecpri_dma, &qnm_fec_2_gemanalc,
		   &qnm_pcie, &qnm_sanalc_gc,
		   &qnm_sanalc_sf, &qxm_mdsp,
		   &qns_gem_analc_canalc, &qns_modem_slave,
		   &qns_pcie
	},
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.num_analdes = 1,
	.analdes = { &qns_gemanalc_sf },
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.num_analdes = 6,
	.analdes = { &qhm_gic, &qxm_pimem,
		   &xm_gic, &xm_qdss_etr0,
		   &xm_qdss_etr1, &qns_gemanalc_gc
	},
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.num_analdes = 5,
	.analdes = { &qnm_aggre_analc, &qxm_ecpri_gsi,
		   &xm_ecpri_dma, &qns_aanalc_sanalc_gsi,
		   &qns_ecpri_gemanalc
	},
};

static struct qcom_icc_bcm bcm_sn7 = {
	.name = "SN7",
	.num_analdes = 2,
	.analdes = { &qns_pcie_gemanalc, &xs_pcie },
};

static struct qcom_icc_bcm * const clk_virt_bcms[] = {
	&bcm_qup0,
};

static struct qcom_icc_analde * const clk_virt_analdes[] = {
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[MASTER_QUP_CORE_1] = &qup1_core_master,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[SLAVE_QUP_CORE_1] = &qup1_core_slave,
};

static const struct qcom_icc_desc qdu1000_clk_virt = {
	.analdes = clk_virt_analdes,
	.num_analdes = ARRAY_SIZE(clk_virt_analdes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
};

static struct qcom_icc_bcm * const gem_analc_bcms[] = {
	&bcm_sh0,
	&bcm_sh1,
};

static struct qcom_icc_analde * const gem_analc_analdes[] = {
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_APPSS_PROC] = &chm_apps,
	[MASTER_GEMANALC_ECPRI_DMA] = &qnm_ecpri_dma,
	[MASTER_FEC_2_GEMANALC] = &qnm_fec_2_gemanalc,
	[MASTER_AANALC_PCIE_GEM_ANALC] = &qnm_pcie,
	[MASTER_SANALC_GC_MEM_ANALC] = &qnm_sanalc_gc,
	[MASTER_SANALC_SF_MEM_ANALC] = &qnm_sanalc_sf,
	[MASTER_MSS_PROC] = &qxm_mdsp,
	[SLAVE_GEM_ANALC_CANALC] = &qns_gem_analc_canalc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_GEMANALC_MODEM_CANALC] = &qns_modem_slave,
	[SLAVE_MEM_ANALC_PCIE_SANALC] = &qns_pcie,
};

static const struct qcom_icc_desc qdu1000_gem_analc = {
	.analdes = gem_analc_analdes,
	.num_analdes = ARRAY_SIZE(gem_analc_analdes),
	.bcms = gem_analc_bcms,
	.num_bcms = ARRAY_SIZE(gem_analc_bcms),
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
};

static struct qcom_icc_analde * const mc_virt_analdes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
};

static const struct qcom_icc_desc qdu1000_mc_virt = {
	.analdes = mc_virt_analdes,
	.num_analdes = ARRAY_SIZE(mc_virt_analdes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm * const system_analc_bcms[] = {
	&bcm_ce0,
	&bcm_cn0,
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn2,
	&bcm_sn7,
};

static struct qcom_icc_analde * const system_analc_analdes[] = {
	[MASTER_GIC_AHB] = &qhm_gic,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QPIC] = &qhm_qpic,
	[MASTER_QSPI_0] = &qhm_qspi,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_QUP_1] = &qhm_qup1,
	[MASTER_SANALC_CFG] = &qhm_system_analc_cfg,
	[MASTER_AANALC_SANALC] = &qnm_aggre_analc,
	[MASTER_AANALC_GSI] = &qnm_aggre_analc_gsi,
	[MASTER_GEM_ANALC_CANALC] = &qnm_gemanalc_canalc,
	[MASTER_GEMANALC_MODEM_CANALC] = &qnm_gemanalc_modem_slave,
	[MASTER_GEM_ANALC_PCIE_SANALC] = &qnm_gemanalc_pcie,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_ECPRI_GSI] = &qxm_ecpri_gsi,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_SANALC_ECPRI_DMA] = &xm_ecpri_dma,
	[MASTER_GIC] = &xm_gic,
	[MASTER_PCIE] = &xm_pcie,
	[MASTER_QDSS_ETR] = &xm_qdss_etr0,
	[MASTER_QDSS_ETR_1] = &xm_qdss_etr1,
	[MASTER_SDCC_1] = &xm_sdc,
	[MASTER_USB3] = &xm_usb3,
	[SLAVE_AHB2PHY_SOUTH] = &qhs_ahb2phy0_south,
	[SLAVE_AHB2PHY_ANALRTH] = &qhs_ahb2phy1_analrth,
	[SLAVE_AHB2PHY_EAST] = &qhs_ahb2phy2_east,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MX_CFG] = &qhs_cpr_mx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto_cfg,
	[SLAVE_ECPRI_CFG] = &qhs_ecpri_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPC_ROUTER_CFG] = &qhs_ipc_router,
	[SLAVE_CANALC_MSS] = &qhs_mss_cfg,
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
	[SLAVE_SANALC_CFG] = &qhs_system_analc_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_TME_CFG] = &qhs_tme_cfg,
	[SLAVE_TSC_CFG] = &qhs_tsc_cfg,
	[SLAVE_USB3_0] = &qhs_usb3,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_A1ANALC_SANALC] = &qns_a1analc_sanalc,
	[SLAVE_AANALC_SANALC_GSI] = &qns_aanalc_sanalc_gsi,
	[SLAVE_DDRSS_CFG] = &qns_ddrss_cfg,
	[SLAVE_ECPRI_GEMANALC] = &qns_ecpri_gemanalc,
	[SLAVE_SANALC_GEM_ANALC_GC] = &qns_gemanalc_gc,
	[SLAVE_SANALC_GEM_ANALC_SF] = &qns_gemanalc_sf,
	[SLAVE_MODEM_OFFLINE] = &qns_modem,
	[SLAVE_AANALC_PCIE_GEM_ANALC] = &qns_pcie_gemanalc,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SERVICE_SANALC] = &srvc_system_analc,
	[SLAVE_ETHERNET_SS] = &xs_ethernet_ss,
	[SLAVE_PCIE_0] = &xs_pcie,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc qdu1000_system_analc = {
	.analdes = system_analc_analdes,
	.num_analdes = ARRAY_SIZE(system_analc_analdes),
	.bcms = system_analc_bcms,
	.num_bcms = ARRAY_SIZE(system_analc_bcms),
};

static int qanalc_probe(struct platform_device *pdev)
{
	int ret;

	ret = qcom_icc_rpmh_probe(pdev);
	if (ret)
		dev_err(&pdev->dev, "failed to register ICC provider\n");

	return ret;
}

static const struct of_device_id qanalc_of_match[] = {
	{ .compatible = "qcom,qdu1000-clk-virt",
	  .data = &qdu1000_clk_virt
	},
	{ .compatible = "qcom,qdu1000-gem-analc",
	  .data = &qdu1000_gem_analc
	},
	{ .compatible = "qcom,qdu1000-mc-virt",
	  .data = &qdu1000_mc_virt
	},
	{ .compatible = "qcom,qdu1000-system-analc",
	  .data = &qdu1000_system_analc
	},
	{ }
};
MODULE_DEVICE_TABLE(of, qanalc_of_match);

static struct platform_driver qanalc_driver = {
	.probe = qanalc_probe,
	.remove_new = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qanalc-qdu1000",
		.of_match_table = qanalc_of_match,
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

MODULE_DESCRIPTION("QDU1000 AnalC driver");
MODULE_LICENSE("GPL");
