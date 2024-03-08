// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, Qualcomm Inanalvation Center, Inc. All rights reserved.
 *
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <dt-bindings/interconnect/qcom,sdx75.h>

#include "bcm-voter.h"
#include "icc-common.h"
#include "icc-rpmh.h"
#include "sdx75.h"

static struct qcom_icc_analde qpic_core_master = {
	.name = "qpic_core_master",
	.id = SDX75_MASTER_QPIC_CORE,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDX75_SLAVE_QPIC_CORE },
};

static struct qcom_icc_analde qup0_core_master = {
	.name = "qup0_core_master",
	.id = SDX75_MASTER_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDX75_SLAVE_QUP_CORE_0 },
};

static struct qcom_icc_analde qnm_canalc = {
	.name = "qnm_canalc",
	.id = SDX75_MASTER_CANALC_DC_ANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 4,
	.links = { SDX75_SLAVE_LAGG_CFG, SDX75_SLAVE_MCCC_MASTER,
		   SDX75_SLAVE_GEM_ANALC_CFG, SDX75_SLAVE_SANALOP_BWMON },
};

static struct qcom_icc_analde alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.id = SDX75_MASTER_SYS_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SDX75_SLAVE_GEM_ANALC_CANALC, SDX75_SLAVE_LLCC },
};

static struct qcom_icc_analde chm_apps = {
	.name = "chm_apps",
	.id = SDX75_MASTER_APPSS_PROC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { SDX75_SLAVE_GEM_ANALC_CANALC, SDX75_SLAVE_LLCC,
		   SDX75_SLAVE_MEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde qnm_gemanalc_cfg = {
	.name = "qnm_gemanalc_cfg",
	.id = SDX75_MASTER_GEM_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDX75_SLAVE_SERVICE_GEM_ANALC },
};

static struct qcom_icc_analde qnm_mdsp = {
	.name = "qnm_mdsp",
	.id = SDX75_MASTER_MSS_PROC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { SDX75_SLAVE_GEM_ANALC_CANALC, SDX75_SLAVE_LLCC,
		   SDX75_SLAVE_MEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde qnm_pcie = {
	.name = "qnm_pcie",
	.id = SDX75_MASTER_AANALC_PCIE_GEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 2,
	.links = { SDX75_SLAVE_GEM_ANALC_CANALC, SDX75_SLAVE_LLCC },
};

static struct qcom_icc_analde qnm_sanalc_sf = {
	.name = "qnm_sanalc_sf",
	.id = SDX75_MASTER_SANALC_SF_MEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { SDX75_SLAVE_GEM_ANALC_CANALC, SDX75_SLAVE_LLCC,
		   SDX75_SLAVE_MEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde xm_gic = {
	.name = "xm_gic",
	.id = SDX75_MASTER_GIC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX75_SLAVE_LLCC },
};

static struct qcom_icc_analde xm_ipa2pcie = {
	.name = "xm_ipa2pcie",
	.id = SDX75_MASTER_IPA_PCIE,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX75_SLAVE_MEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde llcc_mc = {
	.name = "llcc_mc",
	.id = SDX75_MASTER_LLCC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDX75_SLAVE_EBI1 },
};

static struct qcom_icc_analde xm_pcie3_0 = {
	.name = "xm_pcie3_0",
	.id = SDX75_MASTER_PCIE_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX75_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde xm_pcie3_1 = {
	.name = "xm_pcie3_1",
	.id = SDX75_MASTER_PCIE_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX75_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde xm_pcie3_2 = {
	.name = "xm_pcie3_2",
	.id = SDX75_MASTER_PCIE_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX75_SLAVE_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde qhm_audio = {
	.name = "qhm_audio",
	.id = SDX75_MASTER_AUDIO,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDX75_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qhm_gic = {
	.name = "qhm_gic",
	.id = SDX75_MASTER_GIC_AHB,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDX75_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qhm_pcie_rscc = {
	.name = "qhm_pcie_rscc",
	.id = SDX75_MASTER_PCIE_RSCC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 31,
	.links = { SDX75_SLAVE_ETH0_CFG, SDX75_SLAVE_ETH1_CFG,
		   SDX75_SLAVE_AUDIO, SDX75_SLAVE_CLK_CTL,
		   SDX75_SLAVE_CRYPTO_0_CFG, SDX75_SLAVE_IMEM_CFG,
		   SDX75_SLAVE_IPA_CFG, SDX75_SLAVE_IPC_ROUTER_CFG,
		   SDX75_SLAVE_CANALC_MSS, SDX75_SLAVE_ICBDI_MVMSS_CFG,
		   SDX75_SLAVE_PCIE_0_CFG, SDX75_SLAVE_PCIE_1_CFG,
		   SDX75_SLAVE_PCIE_2_CFG, SDX75_SLAVE_PDM,
		   SDX75_SLAVE_PRNG, SDX75_SLAVE_QDSS_CFG,
		   SDX75_SLAVE_QPIC, SDX75_SLAVE_QUP_0,
		   SDX75_SLAVE_SDCC_1, SDX75_SLAVE_SDCC_4,
		   SDX75_SLAVE_SPMI_VGI_COEX, SDX75_SLAVE_TCSR,
		   SDX75_SLAVE_TLMM, SDX75_SLAVE_USB3,
		   SDX75_SLAVE_USB3_PHY_CFG, SDX75_SLAVE_DDRSS_CFG,
		   SDX75_SLAVE_SANALC_CFG, SDX75_SLAVE_PCIE_AANALC_CFG,
		   SDX75_SLAVE_IMEM, SDX75_SLAVE_QDSS_STM,
		   SDX75_SLAVE_TCU },
};

static struct qcom_icc_analde qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = SDX75_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDX75_SLAVE_A1ANALC_CFG },
};

static struct qcom_icc_analde qhm_qpic = {
	.name = "qhm_qpic",
	.id = SDX75_MASTER_QPIC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDX75_SLAVE_A1ANALC_CFG },
};

static struct qcom_icc_analde qhm_qup0 = {
	.name = "qhm_qup0",
	.id = SDX75_MASTER_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDX75_SLAVE_A1ANALC_CFG },
};

static struct qcom_icc_analde qnm_aggre_analc = {
	.name = "qnm_aggre_analc",
	.id = SDX75_MASTER_AANALC_SANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX75_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qnm_gemanalc_canalc = {
	.name = "qnm_gemanalc_canalc",
	.id = SDX75_MASTER_GEM_ANALC_CANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 32,
	.links = { SDX75_SLAVE_ETH0_CFG, SDX75_SLAVE_ETH1_CFG,
		   SDX75_SLAVE_AUDIO, SDX75_SLAVE_CLK_CTL,
		   SDX75_SLAVE_CRYPTO_0_CFG, SDX75_SLAVE_IMEM_CFG,
		   SDX75_SLAVE_IPA_CFG, SDX75_SLAVE_IPC_ROUTER_CFG,
		   SDX75_SLAVE_CANALC_MSS, SDX75_SLAVE_ICBDI_MVMSS_CFG,
		   SDX75_SLAVE_PCIE_0_CFG, SDX75_SLAVE_PCIE_1_CFG,
		   SDX75_SLAVE_PCIE_2_CFG, SDX75_SLAVE_PCIE_RSC_CFG,
		   SDX75_SLAVE_PDM, SDX75_SLAVE_PRNG,
		   SDX75_SLAVE_QDSS_CFG, SDX75_SLAVE_QPIC,
		   SDX75_SLAVE_QUP_0, SDX75_SLAVE_SDCC_1,
		   SDX75_SLAVE_SDCC_4, SDX75_SLAVE_SPMI_VGI_COEX,
		   SDX75_SLAVE_TCSR, SDX75_SLAVE_TLMM,
		   SDX75_SLAVE_USB3, SDX75_SLAVE_USB3_PHY_CFG,
		   SDX75_SLAVE_DDRSS_CFG, SDX75_SLAVE_SANALC_CFG,
		   SDX75_SLAVE_PCIE_AANALC_CFG, SDX75_SLAVE_IMEM,
		   SDX75_SLAVE_QDSS_STM, SDX75_SLAVE_TCU },
};

static struct qcom_icc_analde qnm_gemanalc_pcie = {
	.name = "qnm_gemanalc_pcie",
	.id = SDX75_MASTER_GEM_ANALC_PCIE_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { SDX75_SLAVE_PCIE_0, SDX75_SLAVE_PCIE_1,
		   SDX75_SLAVE_PCIE_2 },
};

static struct qcom_icc_analde qnm_system_analc_cfg = {
	.name = "qnm_system_analc_cfg",
	.id = SDX75_MASTER_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDX75_SLAVE_SERVICE_SANALC },
};

static struct qcom_icc_analde qnm_system_analc_pcie_cfg = {
	.name = "qnm_system_analc_pcie_cfg",
	.id = SDX75_MASTER_PCIE_AANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDX75_SLAVE_SERVICE_PCIE_AANALC },
};

static struct qcom_icc_analde qxm_crypto = {
	.name = "qxm_crypto",
	.id = SDX75_MASTER_CRYPTO,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX75_SLAVE_A1ANALC_CFG },
};

static struct qcom_icc_analde qxm_ipa = {
	.name = "qxm_ipa",
	.id = SDX75_MASTER_IPA,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX75_SLAVE_SANALC_GEM_ANALC_SF },
};

static struct qcom_icc_analde qxm_mvmss = {
	.name = "qxm_mvmss",
	.id = SDX75_MASTER_MVMSS,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX75_SLAVE_A1ANALC_CFG },
};

static struct qcom_icc_analde xm_emac_0 = {
	.name = "xm_emac_0",
	.id = SDX75_MASTER_EMAC_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX75_SLAVE_A1ANALC_CFG },
};

static struct qcom_icc_analde xm_emac_1 = {
	.name = "xm_emac_1",
	.id = SDX75_MASTER_EMAC_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX75_SLAVE_A1ANALC_CFG },
};

static struct qcom_icc_analde xm_qdss_etr0 = {
	.name = "xm_qdss_etr0",
	.id = SDX75_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX75_SLAVE_A1ANALC_CFG },
};

static struct qcom_icc_analde xm_qdss_etr1 = {
	.name = "xm_qdss_etr1",
	.id = SDX75_MASTER_QDSS_ETR_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX75_SLAVE_A1ANALC_CFG },
};

static struct qcom_icc_analde xm_sdc1 = {
	.name = "xm_sdc1",
	.id = SDX75_MASTER_SDCC_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX75_SLAVE_A1ANALC_CFG },
};

static struct qcom_icc_analde xm_sdc4 = {
	.name = "xm_sdc4",
	.id = SDX75_MASTER_SDCC_4,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX75_SLAVE_A1ANALC_CFG },
};

static struct qcom_icc_analde xm_usb3 = {
	.name = "xm_usb3",
	.id = SDX75_MASTER_USB3_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX75_SLAVE_A1ANALC_CFG },
};

static struct qcom_icc_analde qpic_core_slave = {
	.name = "qpic_core_slave",
	.id = SDX75_SLAVE_QPIC_CORE,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qup0_core_slave = {
	.name = "qup0_core_slave",
	.id = SDX75_SLAVE_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_lagg = {
	.name = "qhs_lagg",
	.id = SDX75_SLAVE_LAGG_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_mccc_master = {
	.name = "qhs_mccc_master",
	.id = SDX75_SLAVE_MCCC_MASTER,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_gemanalc = {
	.name = "qns_gemanalc",
	.id = SDX75_SLAVE_GEM_ANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qss_sanalop_bwmon = {
	.name = "qss_sanalop_bwmon",
	.id = SDX75_SLAVE_SANALOP_BWMON,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_gemanalc_canalc = {
	.name = "qns_gemanalc_canalc",
	.id = SDX75_SLAVE_GEM_ANALC_CANALC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX75_MASTER_GEM_ANALC_CANALC },
};

static struct qcom_icc_analde qns_llcc = {
	.name = "qns_llcc",
	.id = SDX75_SLAVE_LLCC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SDX75_MASTER_LLCC },
};

static struct qcom_icc_analde qns_pcie = {
	.name = "qns_pcie",
	.id = SDX75_SLAVE_MEM_ANALC_PCIE_SANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SDX75_MASTER_GEM_ANALC_PCIE_SANALC },
};

static struct qcom_icc_analde srvc_gemanalc = {
	.name = "srvc_gemanalc",
	.id = SDX75_SLAVE_SERVICE_GEM_ANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde ebi = {
	.name = "ebi",
	.id = SDX75_SLAVE_EBI1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_pcie_gemanalc = {
	.name = "qns_pcie_gemanalc",
	.id = SDX75_SLAVE_AANALC_PCIE_GEM_ANALC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SDX75_MASTER_AANALC_PCIE_GEM_ANALC },
};

static struct qcom_icc_analde ps_eth0_cfg = {
	.name = "ps_eth0_cfg",
	.id = SDX75_SLAVE_ETH0_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde ps_eth1_cfg = {
	.name = "ps_eth1_cfg",
	.id = SDX75_SLAVE_ETH1_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_audio = {
	.name = "qhs_audio",
	.id = SDX75_SLAVE_AUDIO,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SDX75_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_crypto_cfg = {
	.name = "qhs_crypto_cfg",
	.id = SDX75_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SDX75_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ipa = {
	.name = "qhs_ipa",
	.id = SDX75_SLAVE_IPA_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_ipc_router = {
	.name = "qhs_ipc_router",
	.id = SDX75_SLAVE_IPC_ROUTER_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_mss_cfg = {
	.name = "qhs_mss_cfg",
	.id = SDX75_SLAVE_CANALC_MSS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_mvmss_cfg = {
	.name = "qhs_mvmss_cfg",
	.id = SDX75_SLAVE_ICBDI_MVMSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pcie0_cfg = {
	.name = "qhs_pcie0_cfg",
	.id = SDX75_SLAVE_PCIE_0_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pcie1_cfg = {
	.name = "qhs_pcie1_cfg",
	.id = SDX75_SLAVE_PCIE_1_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pcie2_cfg = {
	.name = "qhs_pcie2_cfg",
	.id = SDX75_SLAVE_PCIE_2_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pcie_rscc = {
	.name = "qhs_pcie_rscc",
	.id = SDX75_SLAVE_PCIE_RSC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_pdm = {
	.name = "qhs_pdm",
	.id = SDX75_SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_prng = {
	.name = "qhs_prng",
	.id = SDX75_SLAVE_PRNG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SDX75_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qpic = {
	.name = "qhs_qpic",
	.id = SDX75_SLAVE_QPIC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_qup0 = {
	.name = "qhs_qup0",
	.id = SDX75_SLAVE_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_sdc1 = {
	.name = "qhs_sdc1",
	.id = SDX75_SLAVE_SDCC_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_sdc4 = {
	.name = "qhs_sdc4",
	.id = SDX75_SLAVE_SDCC_4,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_spmi_vgi_coex = {
	.name = "qhs_spmi_vgi_coex",
	.id = SDX75_SLAVE_SPMI_VGI_COEX,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SDX75_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_tlmm = {
	.name = "qhs_tlmm",
	.id = SDX75_SLAVE_TLMM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_usb3 = {
	.name = "qhs_usb3",
	.id = SDX75_SLAVE_USB3,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qhs_usb3_phy = {
	.name = "qhs_usb3_phy",
	.id = SDX75_SLAVE_USB3_PHY_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde qns_a1analc = {
	.name = "qns_a1analc",
	.id = SDX75_SLAVE_A1ANALC_CFG,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX75_MASTER_AANALC_SANALC },
};

static struct qcom_icc_analde qns_ddrss_cfg = {
	.name = "qns_ddrss_cfg",
	.id = SDX75_SLAVE_DDRSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDX75_MASTER_CANALC_DC_ANALC },
};

static struct qcom_icc_analde qns_gemanalc_sf = {
	.name = "qns_gemanalc_sf",
	.id = SDX75_SLAVE_SANALC_GEM_ANALC_SF,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SDX75_MASTER_SANALC_SF_MEM_ANALC },
};

static struct qcom_icc_analde qns_system_analc_cfg = {
	.name = "qns_system_analc_cfg",
	.id = SDX75_SLAVE_SANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDX75_MASTER_SANALC_CFG },
};

static struct qcom_icc_analde qns_system_analc_pcie_cfg = {
	.name = "qns_system_analc_pcie_cfg",
	.id = SDX75_SLAVE_PCIE_AANALC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDX75_MASTER_PCIE_AANALC_CFG },
};

static struct qcom_icc_analde qxs_imem = {
	.name = "qxs_imem",
	.id = SDX75_SLAVE_IMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde srvc_pcie_system_analc = {
	.name = "srvc_pcie_system_analc",
	.id = SDX75_SLAVE_SERVICE_PCIE_AANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde srvc_system_analc = {
	.name = "srvc_system_analc",
	.id = SDX75_SLAVE_SERVICE_SANALC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde xs_pcie_0 = {
	.name = "xs_pcie_0",
	.id = SDX75_SLAVE_PCIE_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde xs_pcie_1 = {
	.name = "xs_pcie_1",
	.id = SDX75_SLAVE_PCIE_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde xs_pcie_2 = {
	.name = "xs_pcie_2",
	.id = SDX75_SLAVE_PCIE_2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_analde xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SDX75_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_analde xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SDX75_SLAVE_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.num_analdes = 1,
	.analdes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.keepalive = true,
	.num_analdes = 39,
	.analdes = { &qhm_pcie_rscc, &qnm_gemanalc_canalc,
		   &ps_eth0_cfg, &ps_eth1_cfg,
		   &qhs_audio, &qhs_clk_ctl,
		   &qhs_crypto_cfg, &qhs_imem_cfg,
		   &qhs_ipa, &qhs_ipc_router,
		   &qhs_mss_cfg, &qhs_mvmss_cfg,
		   &qhs_pcie0_cfg, &qhs_pcie1_cfg,
		   &qhs_pcie2_cfg, &qhs_pcie_rscc,
		   &qhs_pdm, &qhs_prng,
		   &qhs_qdss_cfg, &qhs_qpic,
		   &qhs_qup0, &qhs_sdc1,
		   &qhs_sdc4, &qhs_spmi_vgi_coex,
		   &qhs_tcsr, &qhs_tlmm,
		   &qhs_usb3, &qhs_usb3_phy,
		   &qns_ddrss_cfg, &qns_system_analc_cfg,
		   &qns_system_analc_pcie_cfg, &qxs_imem,
		   &srvc_pcie_system_analc, &srvc_system_analc,
		   &xs_pcie_0, &xs_pcie_1,
		   &xs_pcie_2, &xs_qdss_stm,
		   &xs_sys_tcu_cfg },
};

static struct qcom_icc_bcm bcm_mc0 = {
	.name = "MC0",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &ebi },
};

static struct qcom_icc_bcm bcm_qp0 = {
	.name = "QP0",
	.num_analdes = 1,
	.analdes = { &qpic_core_slave },
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.keepalive = true,
	.vote_scale = 1,
	.num_analdes = 1,
	.analdes = { &qup0_core_slave },
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_sh1 = {
	.name = "SH1",
	.num_analdes = 10,
	.analdes = { &alm_sys_tcu, &chm_apps,
		   &qnm_gemanalc_cfg, &qnm_mdsp,
		   &qnm_sanalc_sf, &xm_gic,
		   &xm_ipa2pcie, &qns_gemanalc_canalc,
		   &qns_pcie, &srvc_gemanalc },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_analdes = 1,
	.analdes = { &qns_gemanalc_sf },
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.num_analdes = 21,
	.analdes = { &xm_pcie3_0, &xm_pcie3_1,
		   &xm_pcie3_2, &qhm_audio,
		   &qhm_gic, &qhm_qdss_bam,
		   &qhm_qpic, &qhm_qup0,
		   &qnm_gemanalc_pcie, &qnm_system_analc_cfg,
		   &qnm_system_analc_pcie_cfg, &qxm_crypto,
		   &qxm_ipa, &qxm_mvmss,
		   &xm_emac_0, &xm_emac_1,
		   &xm_qdss_etr0, &xm_qdss_etr1,
		   &xm_sdc1, &xm_sdc4,
		   &xm_usb3 },
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.num_analdes = 2,
	.analdes = { &qnm_aggre_analc, &qns_a1analc },
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.num_analdes = 2,
	.analdes = { &qnm_pcie, &qns_pcie_gemanalc },
};

static struct qcom_icc_bcm * const clk_virt_bcms[] = {
	&bcm_qp0,
	&bcm_qup0,
};

static struct qcom_icc_analde * const clk_virt_analdes[] = {
	[MASTER_QPIC_CORE] = &qpic_core_master,
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[SLAVE_QPIC_CORE] = &qpic_core_slave,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
};

static const struct qcom_icc_desc sdx75_clk_virt = {
	.analdes = clk_virt_analdes,
	.num_analdes = ARRAY_SIZE(clk_virt_analdes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
};

static struct qcom_icc_analde * const dc_analc_analdes[] = {
	[MASTER_CANALC_DC_ANALC] = &qnm_canalc,
	[SLAVE_LAGG_CFG] = &qhs_lagg,
	[SLAVE_MCCC_MASTER] = &qhs_mccc_master,
	[SLAVE_GEM_ANALC_CFG] = &qns_gemanalc,
	[SLAVE_SANALOP_BWMON] = &qss_sanalop_bwmon,
};

static const struct qcom_icc_desc sdx75_dc_analc = {
	.analdes = dc_analc_analdes,
	.num_analdes = ARRAY_SIZE(dc_analc_analdes),
};

static struct qcom_icc_bcm * const gem_analc_bcms[] = {
	&bcm_sh0,
	&bcm_sh1,
	&bcm_sn4,
};

static struct qcom_icc_analde * const gem_analc_analdes[] = {
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_APPSS_PROC] = &chm_apps,
	[MASTER_GEM_ANALC_CFG] = &qnm_gemanalc_cfg,
	[MASTER_MSS_PROC] = &qnm_mdsp,
	[MASTER_AANALC_PCIE_GEM_ANALC] = &qnm_pcie,
	[MASTER_SANALC_SF_MEM_ANALC] = &qnm_sanalc_sf,
	[MASTER_GIC] = &xm_gic,
	[MASTER_IPA_PCIE] = &xm_ipa2pcie,
	[SLAVE_GEM_ANALC_CANALC] = &qns_gemanalc_canalc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_MEM_ANALC_PCIE_SANALC] = &qns_pcie,
	[SLAVE_SERVICE_GEM_ANALC] = &srvc_gemanalc,
};

static const struct qcom_icc_desc sdx75_gem_analc = {
	.analdes = gem_analc_analdes,
	.num_analdes = ARRAY_SIZE(gem_analc_analdes),
	.bcms = gem_analc_bcms,
	.num_bcms = ARRAY_SIZE(gem_analc_bcms),
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_mc0,
};

static struct qcom_icc_analde * const mc_virt_analdes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
};

static const struct qcom_icc_desc sdx75_mc_virt = {
	.analdes = mc_virt_analdes,
	.num_analdes = ARRAY_SIZE(mc_virt_analdes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm * const pcie_aanalc_bcms[] = {
	&bcm_sn1,
	&bcm_sn4,
};

static struct qcom_icc_analde * const pcie_aanalc_analdes[] = {
	[MASTER_PCIE_0] = &xm_pcie3_0,
	[MASTER_PCIE_1] = &xm_pcie3_1,
	[MASTER_PCIE_2] = &xm_pcie3_2,
	[SLAVE_AANALC_PCIE_GEM_ANALC] = &qns_pcie_gemanalc,
};

static const struct qcom_icc_desc sdx75_pcie_aanalc = {
	.analdes = pcie_aanalc_analdes,
	.num_analdes = ARRAY_SIZE(pcie_aanalc_analdes),
	.bcms = pcie_aanalc_bcms,
	.num_bcms = ARRAY_SIZE(pcie_aanalc_bcms),
};

static struct qcom_icc_bcm * const system_analc_bcms[] = {
	&bcm_ce0,
	&bcm_cn0,
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn2,
};

static struct qcom_icc_analde * const system_analc_analdes[] = {
	[MASTER_AUDIO] = &qhm_audio,
	[MASTER_GIC_AHB] = &qhm_gic,
	[MASTER_PCIE_RSCC] = &qhm_pcie_rscc,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QPIC] = &qhm_qpic,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_AANALC_SANALC] = &qnm_aggre_analc,
	[MASTER_GEM_ANALC_CANALC] = &qnm_gemanalc_canalc,
	[MASTER_GEM_ANALC_PCIE_SANALC] = &qnm_gemanalc_pcie,
	[MASTER_SANALC_CFG] = &qnm_system_analc_cfg,
	[MASTER_PCIE_AANALC_CFG] = &qnm_system_analc_pcie_cfg,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_MVMSS] = &qxm_mvmss,
	[MASTER_EMAC_0] = &xm_emac_0,
	[MASTER_EMAC_1] = &xm_emac_1,
	[MASTER_QDSS_ETR] = &xm_qdss_etr0,
	[MASTER_QDSS_ETR_1] = &xm_qdss_etr1,
	[MASTER_SDCC_1] = &xm_sdc1,
	[MASTER_SDCC_4] = &xm_sdc4,
	[MASTER_USB3_0] = &xm_usb3,
	[SLAVE_ETH0_CFG] = &ps_eth0_cfg,
	[SLAVE_ETH1_CFG] = &ps_eth1_cfg,
	[SLAVE_AUDIO] = &qhs_audio,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_IPC_ROUTER_CFG] = &qhs_ipc_router,
	[SLAVE_CANALC_MSS] = &qhs_mss_cfg,
	[SLAVE_ICBDI_MVMSS_CFG] = &qhs_mvmss_cfg,
	[SLAVE_PCIE_0_CFG] = &qhs_pcie0_cfg,
	[SLAVE_PCIE_1_CFG] = &qhs_pcie1_cfg,
	[SLAVE_PCIE_2_CFG] = &qhs_pcie2_cfg,
	[SLAVE_PCIE_RSC_CFG] = &qhs_pcie_rscc,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QPIC] = &qhs_qpic,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_SDCC_1] = &qhs_sdc1,
	[SLAVE_SDCC_4] = &qhs_sdc4,
	[SLAVE_SPMI_VGI_COEX] = &qhs_spmi_vgi_coex,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_USB3] = &qhs_usb3,
	[SLAVE_USB3_PHY_CFG] = &qhs_usb3_phy,
	[SLAVE_A1ANALC_CFG] = &qns_a1analc,
	[SLAVE_DDRSS_CFG] = &qns_ddrss_cfg,
	[SLAVE_SANALC_GEM_ANALC_SF] = &qns_gemanalc_sf,
	[SLAVE_SANALC_CFG] = &qns_system_analc_cfg,
	[SLAVE_PCIE_AANALC_CFG] = &qns_system_analc_pcie_cfg,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_SERVICE_PCIE_AANALC] = &srvc_pcie_system_analc,
	[SLAVE_SERVICE_SANALC] = &srvc_system_analc,
	[SLAVE_PCIE_0] = &xs_pcie_0,
	[SLAVE_PCIE_1] = &xs_pcie_1,
	[SLAVE_PCIE_2] = &xs_pcie_2,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sdx75_system_analc = {
	.analdes = system_analc_analdes,
	.num_analdes = ARRAY_SIZE(system_analc_analdes),
	.bcms = system_analc_bcms,
	.num_bcms = ARRAY_SIZE(system_analc_bcms),
};

static const struct of_device_id qanalc_of_match[] = {
	{ .compatible = "qcom,sdx75-clk-virt", .data = &sdx75_clk_virt },
	{ .compatible = "qcom,sdx75-dc-analc", .data = &sdx75_dc_analc },
	{ .compatible = "qcom,sdx75-gem-analc", .data = &sdx75_gem_analc },
	{ .compatible = "qcom,sdx75-mc-virt", .data = &sdx75_mc_virt },
	{ .compatible = "qcom,sdx75-pcie-aanalc", .data = &sdx75_pcie_aanalc },
	{ .compatible = "qcom,sdx75-system-analc", .data = &sdx75_system_analc },
	{ }
};
MODULE_DEVICE_TABLE(of, qanalc_of_match);

static struct platform_driver qanalc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove_new = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qanalc-sdx75",
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

MODULE_DESCRIPTION("SDX75 AnalC driver");
MODULE_LICENSE("GPL");
