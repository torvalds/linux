// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <dt-bindings/interconnect/qcom,sdx65.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"

static struct qcom_icc_node llcc_mc;
static struct qcom_icc_node acm_tcu;
static struct qcom_icc_node qnm_snoc_gc;
static struct qcom_icc_node xm_apps_rdwr;
static struct qcom_icc_node qhm_audio;
static struct qcom_icc_node qhm_blsp1;
static struct qcom_icc_node qhm_qdss_bam;
static struct qcom_icc_node qhm_qpic;
static struct qcom_icc_node qhm_snoc_cfg;
static struct qcom_icc_node qhm_spmi_fetcher1;
static struct qcom_icc_node qnm_aggre_noc;
static struct qcom_icc_node qnm_ipa;
static struct qcom_icc_node qnm_memnoc;
static struct qcom_icc_node qnm_memnoc_pcie;
static struct qcom_icc_node qxm_crypto;
static struct qcom_icc_node xm_ipa2pcie_slv;
static struct qcom_icc_node xm_pcie;
static struct qcom_icc_node xm_qdss_etr;
static struct qcom_icc_node xm_sdc1;
static struct qcom_icc_node xm_usb3;
static struct qcom_icc_node ebi;
static struct qcom_icc_node qns_llcc;
static struct qcom_icc_node qns_memnoc_snoc;
static struct qcom_icc_node qns_sys_pcie;
static struct qcom_icc_node qhs_aoss;
static struct qcom_icc_node qhs_apss;
static struct qcom_icc_node qhs_audio;
static struct qcom_icc_node qhs_blsp1;
static struct qcom_icc_node qhs_clk_ctl;
static struct qcom_icc_node qhs_crypto0_cfg;
static struct qcom_icc_node qhs_ddrss_cfg;
static struct qcom_icc_node qhs_ecc_cfg;
static struct qcom_icc_node qhs_imem_cfg;
static struct qcom_icc_node qhs_ipa;
static struct qcom_icc_node qhs_mss_cfg;
static struct qcom_icc_node qhs_pcie_parf;
static struct qcom_icc_node qhs_pdm;
static struct qcom_icc_node qhs_prng;
static struct qcom_icc_node qhs_qdss_cfg;
static struct qcom_icc_node qhs_qpic;
static struct qcom_icc_node qhs_sdc1;
static struct qcom_icc_node qhs_snoc_cfg;
static struct qcom_icc_node qhs_spmi_fetcher;
static struct qcom_icc_node qhs_spmi_vgi_coex;
static struct qcom_icc_node qhs_tcsr;
static struct qcom_icc_node qhs_tlmm;
static struct qcom_icc_node qhs_usb3;
static struct qcom_icc_node qhs_usb3_phy;
static struct qcom_icc_node qns_aggre_noc;
static struct qcom_icc_node qns_snoc_memnoc;
static struct qcom_icc_node qxs_imem;
static struct qcom_icc_node srvc_snoc;
static struct qcom_icc_node xs_pcie;
static struct qcom_icc_node xs_qdss_stm;
static struct qcom_icc_node xs_sys_tcu_cfg;

static struct qcom_icc_node llcc_mc = {
	.name = "llcc_mc",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &ebi },
};

static struct qcom_icc_node acm_tcu = {
	.name = "acm_tcu",
	.channels = 1,
	.buswidth = 8,
	.num_links = 3,
	.link_nodes = { &qns_llcc,
			&qns_memnoc_snoc,
			&qns_sys_pcie },
};

static struct qcom_icc_node qnm_snoc_gc = {
	.name = "qnm_snoc_gc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qns_llcc },
};

static struct qcom_icc_node xm_apps_rdwr = {
	.name = "xm_apps_rdwr",
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.link_nodes = { &qns_llcc,
			&qns_memnoc_snoc,
			&qns_sys_pcie },
};

static struct qcom_icc_node qhm_audio = {
	.name = "qhm_audio",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qns_aggre_noc },
};

static struct qcom_icc_node qhm_blsp1 = {
	.name = "qhm_blsp1",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qns_aggre_noc },
};

static struct qcom_icc_node qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.channels = 1,
	.buswidth = 4,
	.num_links = 26,
	.link_nodes = { &qhs_aoss,
			&qhs_audio,
			&qhs_blsp1,
			&qhs_clk_ctl,
			&qhs_crypto0_cfg,
			&qhs_ddrss_cfg,
			&qhs_ecc_cfg,
			&qhs_imem_cfg,
			&qhs_ipa,
			&qhs_mss_cfg,
			&qhs_pcie_parf,
			&qhs_pdm,
			&qhs_prng,
			&qhs_qdss_cfg,
			&qhs_qpic,
			&qhs_sdc1,
			&qhs_snoc_cfg,
			&qhs_spmi_fetcher,
			&qhs_spmi_vgi_coex,
			&qhs_tcsr,
			&qhs_tlmm,
			&qhs_usb3,
			&qhs_usb3_phy,
			&qns_snoc_memnoc,
			&qxs_imem,
			&xs_sys_tcu_cfg },
};

static struct qcom_icc_node qhm_qpic = {
	.name = "qhm_qpic",
	.channels = 1,
	.buswidth = 4,
	.num_links = 4,
	.link_nodes = { &qhs_aoss,
			&qhs_audio,
			&qhs_ipa,
			&qns_aggre_noc },
};

static struct qcom_icc_node qhm_snoc_cfg = {
	.name = "qhm_snoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &srvc_snoc },
};

static struct qcom_icc_node qhm_spmi_fetcher1 = {
	.name = "qhm_spmi_fetcher1",
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.link_nodes = { &qhs_aoss,
			&qns_aggre_noc },
};

static struct qcom_icc_node qnm_aggre_noc = {
	.name = "qnm_aggre_noc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 29,
	.link_nodes = { &qhs_aoss,
			&qhs_apss,
			&qhs_audio,
			&qhs_blsp1,
			&qhs_clk_ctl,
			&qhs_crypto0_cfg,
			&qhs_ddrss_cfg,
			&qhs_ecc_cfg,
			&qhs_imem_cfg,
			&qhs_ipa,
			&qhs_mss_cfg,
			&qhs_pcie_parf,
			&qhs_pdm,
			&qhs_prng,
			&qhs_qdss_cfg,
			&qhs_qpic,
			&qhs_sdc1,
			&qhs_snoc_cfg,
			&qhs_spmi_fetcher,
			&qhs_spmi_vgi_coex,
			&qhs_tcsr,
			&qhs_tlmm,
			&qhs_usb3,
			&qhs_usb3_phy,
			&qns_snoc_memnoc,
			&qxs_imem,
			&xs_pcie,
			&xs_qdss_stm,
			&xs_sys_tcu_cfg },
};

static struct qcom_icc_node qnm_ipa = {
	.name = "qnm_ipa",
	.channels = 1,
	.buswidth = 8,
	.num_links = 26,
	.link_nodes = { &qhs_aoss,
			&qhs_audio,
			&qhs_blsp1,
			&qhs_clk_ctl,
			&qhs_crypto0_cfg,
			&qhs_ddrss_cfg,
			&qhs_ecc_cfg,
			&qhs_imem_cfg,
			&qhs_ipa,
			&qhs_mss_cfg,
			&qhs_pcie_parf,
			&qhs_pdm,
			&qhs_prng,
			&qhs_qdss_cfg,
			&qhs_qpic,
			&qhs_sdc1,
			&qhs_snoc_cfg,
			&qhs_spmi_fetcher,
			&qhs_tcsr,
			&qhs_tlmm,
			&qhs_usb3,
			&qhs_usb3_phy,
			&qns_snoc_memnoc,
			&qxs_imem,
			&xs_pcie,
			&xs_qdss_stm },
};

static struct qcom_icc_node qnm_memnoc = {
	.name = "qnm_memnoc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 27,
	.link_nodes = { &qhs_aoss,
			&qhs_apss,
			&qhs_audio,
			&qhs_blsp1,
			&qhs_clk_ctl,
			&qhs_crypto0_cfg,
			&qhs_ddrss_cfg,
			&qhs_ecc_cfg,
			&qhs_imem_cfg,
			&qhs_ipa,
			&qhs_mss_cfg,
			&qhs_pcie_parf,
			&qhs_pdm,
			&qhs_prng,
			&qhs_qdss_cfg,
			&qhs_qpic,
			&qhs_sdc1,
			&qhs_snoc_cfg,
			&qhs_spmi_fetcher,
			&qhs_spmi_vgi_coex,
			&qhs_tcsr,
			&qhs_tlmm,
			&qhs_usb3,
			&qhs_usb3_phy,
			&qxs_imem,
			&xs_qdss_stm,
			&xs_sys_tcu_cfg },
};

static struct qcom_icc_node qnm_memnoc_pcie = {
	.name = "qnm_memnoc_pcie",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &xs_pcie },
};

static struct qcom_icc_node qxm_crypto = {
	.name = "qxm_crypto",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = { &qhs_aoss,
			&qns_aggre_noc },
};

static struct qcom_icc_node xm_ipa2pcie_slv = {
	.name = "xm_ipa2pcie_slv",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &xs_pcie },
};

static struct qcom_icc_node xm_pcie = {
	.name = "xm_pcie",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_aggre_noc },
};

static struct qcom_icc_node xm_qdss_etr = {
	.name = "xm_qdss_etr",
	.channels = 1,
	.buswidth = 8,
	.num_links = 26,
	.link_nodes = { &qhs_aoss,
			&qhs_audio,
			&qhs_blsp1,
			&qhs_clk_ctl,
			&qhs_crypto0_cfg,
			&qhs_ddrss_cfg,
			&qhs_ecc_cfg,
			&qhs_imem_cfg,
			&qhs_ipa,
			&qhs_mss_cfg,
			&qhs_pcie_parf,
			&qhs_pdm,
			&qhs_prng,
			&qhs_qdss_cfg,
			&qhs_qpic,
			&qhs_sdc1,
			&qhs_snoc_cfg,
			&qhs_spmi_fetcher,
			&qhs_spmi_vgi_coex,
			&qhs_tcsr,
			&qhs_tlmm,
			&qhs_usb3,
			&qhs_usb3_phy,
			&qns_snoc_memnoc,
			&qxs_imem,
			&xs_sys_tcu_cfg },
};

static struct qcom_icc_node xm_sdc1 = {
	.name = "xm_sdc1",
	.channels = 1,
	.buswidth = 8,
	.num_links = 4,
	.link_nodes = { &qhs_aoss,
			&qhs_audio,
			&qhs_ipa,
			&qns_aggre_noc },
};

static struct qcom_icc_node xm_usb3 = {
	.name = "xm_usb3",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_aggre_noc },
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_llcc = {
	.name = "qns_llcc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &llcc_mc },
};

static struct qcom_icc_node qns_memnoc_snoc = {
	.name = "qns_memnoc_snoc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qnm_memnoc },
};

static struct qcom_icc_node qns_sys_pcie = {
	.name = "qns_sys_pcie",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qnm_memnoc_pcie },
};

static struct qcom_icc_node qhs_aoss = {
	.name = "qhs_aoss",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_apss = {
	.name = "qhs_apss",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_audio = {
	.name = "qhs_audio",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_blsp1 = {
	.name = "qhs_blsp1",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ddrss_cfg = {
	.name = "qhs_ddrss_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ecc_cfg = {
	.name = "qhs_ecc_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ipa = {
	.name = "qhs_ipa",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_mss_cfg = {
	.name = "qhs_mss_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie_parf = {
	.name = "qhs_pcie_parf",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pdm = {
	.name = "qhs_pdm",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_prng = {
	.name = "qhs_prng",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qpic = {
	.name = "qhs_qpic",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_sdc1 = {
	.name = "qhs_sdc1",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_snoc_cfg = {
	.name = "qhs_snoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qhm_snoc_cfg },
};

static struct qcom_icc_node qhs_spmi_fetcher = {
	.name = "qhs_spmi_fetcher",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_spmi_vgi_coex = {
	.name = "qhs_spmi_vgi_coex",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_tcsr = {
	.name = "qhs_tcsr",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_tlmm = {
	.name = "qhs_tlmm",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb3 = {
	.name = "qhs_usb3",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb3_phy = {
	.name = "qhs_usb3_phy",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_aggre_noc = {
	.name = "qns_aggre_noc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qnm_aggre_noc },
};

static struct qcom_icc_node qns_snoc_memnoc = {
	.name = "qns_snoc_memnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_snoc_gc },
};

static struct qcom_icc_node qxs_imem = {
	.name = "qxs_imem",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node srvc_snoc = {
	.name = "srvc_snoc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node xs_pcie = {
	.name = "xs_pcie",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_mc0 = {
	.name = "MC0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_pn0 = {
	.name = "PN0",
	.keepalive = true,
	.num_nodes = 26,
	.nodes = { &qhm_snoc_cfg,
		   &qhs_aoss,
		   &qhs_apss,
		   &qhs_audio,
		   &qhs_blsp1,
		   &qhs_clk_ctl,
		   &qhs_crypto0_cfg,
		   &qhs_ddrss_cfg,
		   &qhs_ecc_cfg,
		   &qhs_imem_cfg,
		   &qhs_ipa,
		   &qhs_mss_cfg,
		   &qhs_pcie_parf,
		   &qhs_pdm,
		   &qhs_prng,
		   &qhs_qdss_cfg,
		   &qhs_qpic,
		   &qhs_sdc1,
		   &qhs_snoc_cfg,
		   &qhs_spmi_fetcher,
		   &qhs_spmi_vgi_coex,
		   &qhs_tcsr,
		   &qhs_tlmm,
		   &qhs_usb3,
		   &qhs_usb3_phy,
		   &srvc_snoc
	},
};

static struct qcom_icc_bcm bcm_pn1 = {
	.name = "PN1",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &xm_sdc1 },
};

static struct qcom_icc_bcm bcm_pn2 = {
	.name = "PN2",
	.keepalive = false,
	.num_nodes = 2,
	.nodes = { &qhm_audio, &qhm_spmi_fetcher1 },
};

static struct qcom_icc_bcm bcm_pn3 = {
	.name = "PN3",
	.keepalive = false,
	.num_nodes = 2,
	.nodes = { &qhm_blsp1, &qhm_qpic },
};

static struct qcom_icc_bcm bcm_pn4 = {
	.name = "PN4",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_sh1 = {
	.name = "SH1",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qns_memnoc_snoc },
};

static struct qcom_icc_bcm bcm_sh3 = {
	.name = "SH3",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &xm_apps_rdwr },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_snoc_memnoc },
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qxs_imem },
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &xs_qdss_stm },
};

static struct qcom_icc_bcm bcm_sn3 = {
	.name = "SN3",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &xs_sys_tcu_cfg },
};

static struct qcom_icc_bcm bcm_sn5 = {
	.name = "SN5",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &xs_pcie },
};

static struct qcom_icc_bcm bcm_sn6 = {
	.name = "SN6",
	.keepalive = false,
	.num_nodes = 2,
	.nodes = { &qhm_qdss_bam, &xm_qdss_etr },
};

static struct qcom_icc_bcm bcm_sn7 = {
	.name = "SN7",
	.keepalive = false,
	.num_nodes = 4,
	.nodes = { &qnm_aggre_noc, &xm_pcie, &xm_usb3, &qns_aggre_noc },
};

static struct qcom_icc_bcm bcm_sn8 = {
	.name = "SN8",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qnm_memnoc },
};

static struct qcom_icc_bcm bcm_sn9 = {
	.name = "SN9",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qnm_memnoc_pcie },
};

static struct qcom_icc_bcm bcm_sn10 = {
	.name = "SN10",
	.keepalive = false,
	.num_nodes = 2,
	.nodes = { &qnm_ipa, &xm_ipa2pcie_slv },
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_mc0,
};

static struct qcom_icc_node * const mc_virt_nodes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
};

static const struct qcom_icc_desc sdx65_mc_virt = {
	.nodes = mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm * const mem_noc_bcms[] = {
	&bcm_sh0,
	&bcm_sh1,
	&bcm_sh3,
};

static struct qcom_icc_node * const mem_noc_nodes[] = {
	[MASTER_TCU_0] = &acm_tcu,
	[MASTER_SNOC_GC_MEM_NOC] = &qnm_snoc_gc,
	[MASTER_APPSS_PROC] = &xm_apps_rdwr,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_MEM_NOC_SNOC] = &qns_memnoc_snoc,
	[SLAVE_MEM_NOC_PCIE_SNOC] = &qns_sys_pcie,
};

static const struct qcom_icc_desc sdx65_mem_noc = {
	.nodes = mem_noc_nodes,
	.num_nodes = ARRAY_SIZE(mem_noc_nodes),
	.bcms = mem_noc_bcms,
	.num_bcms = ARRAY_SIZE(mem_noc_bcms),
};

static struct qcom_icc_bcm * const system_noc_bcms[] = {
	&bcm_ce0,
	&bcm_pn0,
	&bcm_pn1,
	&bcm_pn2,
	&bcm_pn3,
	&bcm_pn4,
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn2,
	&bcm_sn3,
	&bcm_sn5,
	&bcm_sn6,
	&bcm_sn7,
	&bcm_sn8,
	&bcm_sn9,
	&bcm_sn10,
};

static struct qcom_icc_node * const system_noc_nodes[] = {
	[MASTER_AUDIO] = &qhm_audio,
	[MASTER_BLSP_1] = &qhm_blsp1,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QPIC] = &qhm_qpic,
	[MASTER_SNOC_CFG] = &qhm_snoc_cfg,
	[MASTER_SPMI_FETCHER] = &qhm_spmi_fetcher1,
	[MASTER_ANOC_SNOC] = &qnm_aggre_noc,
	[MASTER_IPA] = &qnm_ipa,
	[MASTER_MEM_NOC_SNOC] = &qnm_memnoc,
	[MASTER_MEM_NOC_PCIE_SNOC] = &qnm_memnoc_pcie,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_IPA_PCIE] = &xm_ipa2pcie_slv,
	[MASTER_PCIE_0] = &xm_pcie,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[MASTER_SDCC_1] = &xm_sdc1,
	[MASTER_USB3] = &xm_usb3,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_APPSS] = &qhs_apss,
	[SLAVE_AUDIO] = &qhs_audio,
	[SLAVE_BLSP_1] = &qhs_blsp1,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_CNOC_DDRSS] = &qhs_ddrss_cfg,
	[SLAVE_ECC_CFG] = &qhs_ecc_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_CNOC_MSS] = &qhs_mss_cfg,
	[SLAVE_PCIE_PARF] = &qhs_pcie_parf,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QPIC] = &qhs_qpic,
	[SLAVE_SDCC_1] = &qhs_sdc1,
	[SLAVE_SNOC_CFG] = &qhs_snoc_cfg,
	[SLAVE_SPMI_FETCHER] = &qhs_spmi_fetcher,
	[SLAVE_SPMI_VGI_COEX] = &qhs_spmi_vgi_coex,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_USB3] = &qhs_usb3,
	[SLAVE_USB3_PHY_CFG] = &qhs_usb3_phy,
	[SLAVE_ANOC_SNOC] = &qns_aggre_noc,
	[SLAVE_SNOC_MEM_NOC_GC] = &qns_snoc_memnoc,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_SERVICE_SNOC] = &srvc_snoc,
	[SLAVE_PCIE_0] = &xs_pcie,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sdx65_system_noc = {
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,sdx65-mc-virt",
	  .data = &sdx65_mc_virt},
	{ .compatible = "qcom,sdx65-mem-noc",
	  .data = &sdx65_mem_noc},
	{ .compatible = "qcom,sdx65-system-noc",
	  .data = &sdx65_system_noc},
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-sdx65",
		.of_match_table = qnoc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(qnoc_driver);

MODULE_DESCRIPTION("Qualcomm SDX65 NoC driver");
MODULE_LICENSE("GPL v2");
