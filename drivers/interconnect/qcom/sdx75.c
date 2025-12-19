// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
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

static struct qcom_icc_node qup0_core_master;
static struct qcom_icc_node qnm_cnoc;
static struct qcom_icc_node alm_sys_tcu;
static struct qcom_icc_node chm_apps;
static struct qcom_icc_node qnm_gemnoc_cfg;
static struct qcom_icc_node qnm_mdsp;
static struct qcom_icc_node qnm_pcie;
static struct qcom_icc_node qnm_snoc_sf;
static struct qcom_icc_node xm_gic;
static struct qcom_icc_node xm_ipa2pcie;
static struct qcom_icc_node llcc_mc;
static struct qcom_icc_node xm_pcie3_0;
static struct qcom_icc_node xm_pcie3_1;
static struct qcom_icc_node xm_pcie3_2;
static struct qcom_icc_node qhm_audio;
static struct qcom_icc_node qhm_gic;
static struct qcom_icc_node qhm_pcie_rscc;
static struct qcom_icc_node qhm_qdss_bam;
static struct qcom_icc_node qhm_qpic;
static struct qcom_icc_node qhm_qup0;
static struct qcom_icc_node qnm_aggre_noc;
static struct qcom_icc_node qnm_gemnoc_cnoc;
static struct qcom_icc_node qnm_gemnoc_pcie;
static struct qcom_icc_node qnm_system_noc_cfg;
static struct qcom_icc_node qnm_system_noc_pcie_cfg;
static struct qcom_icc_node qxm_crypto;
static struct qcom_icc_node qxm_ipa;
static struct qcom_icc_node qxm_mvmss;
static struct qcom_icc_node xm_emac_0;
static struct qcom_icc_node xm_emac_1;
static struct qcom_icc_node xm_qdss_etr0;
static struct qcom_icc_node xm_qdss_etr1;
static struct qcom_icc_node xm_sdc1;
static struct qcom_icc_node xm_sdc4;
static struct qcom_icc_node xm_usb3;
static struct qcom_icc_node qup0_core_slave;
static struct qcom_icc_node qhs_lagg;
static struct qcom_icc_node qhs_mccc_master;
static struct qcom_icc_node qns_gemnoc;
static struct qcom_icc_node qss_snoop_bwmon;
static struct qcom_icc_node qns_gemnoc_cnoc;
static struct qcom_icc_node qns_llcc;
static struct qcom_icc_node qns_pcie;
static struct qcom_icc_node srvc_gemnoc;
static struct qcom_icc_node ebi;
static struct qcom_icc_node qns_pcie_gemnoc;
static struct qcom_icc_node ps_eth0_cfg;
static struct qcom_icc_node ps_eth1_cfg;
static struct qcom_icc_node qhs_audio;
static struct qcom_icc_node qhs_clk_ctl;
static struct qcom_icc_node qhs_crypto_cfg;
static struct qcom_icc_node qhs_imem_cfg;
static struct qcom_icc_node qhs_ipa;
static struct qcom_icc_node qhs_ipc_router;
static struct qcom_icc_node qhs_mss_cfg;
static struct qcom_icc_node qhs_mvmss_cfg;
static struct qcom_icc_node qhs_pcie0_cfg;
static struct qcom_icc_node qhs_pcie1_cfg;
static struct qcom_icc_node qhs_pcie2_cfg;
static struct qcom_icc_node qhs_pcie_rscc;
static struct qcom_icc_node qhs_pdm;
static struct qcom_icc_node qhs_prng;
static struct qcom_icc_node qhs_qdss_cfg;
static struct qcom_icc_node qhs_qpic;
static struct qcom_icc_node qhs_qup0;
static struct qcom_icc_node qhs_sdc1;
static struct qcom_icc_node qhs_sdc4;
static struct qcom_icc_node qhs_spmi_vgi_coex;
static struct qcom_icc_node qhs_tcsr;
static struct qcom_icc_node qhs_tlmm;
static struct qcom_icc_node qhs_usb3;
static struct qcom_icc_node qhs_usb3_phy;
static struct qcom_icc_node qns_a1noc;
static struct qcom_icc_node qns_ddrss_cfg;
static struct qcom_icc_node qns_gemnoc_sf;
static struct qcom_icc_node qns_system_noc_cfg;
static struct qcom_icc_node qns_system_noc_pcie_cfg;
static struct qcom_icc_node qxs_imem;
static struct qcom_icc_node srvc_pcie_system_noc;
static struct qcom_icc_node srvc_system_noc;
static struct qcom_icc_node xs_pcie_0;
static struct qcom_icc_node xs_pcie_1;
static struct qcom_icc_node xs_pcie_2;
static struct qcom_icc_node xs_qdss_stm;
static struct qcom_icc_node xs_sys_tcu_cfg;

static struct qcom_icc_node qup0_core_master = {
	.name = "qup0_core_master",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qup0_core_slave },
};

static struct qcom_icc_node qnm_cnoc = {
	.name = "qnm_cnoc",
	.channels = 1,
	.buswidth = 4,
	.num_links = 4,
	.link_nodes = { &qhs_lagg, &qhs_mccc_master,
			&qns_gemnoc, &qss_snoop_bwmon },
};

static struct qcom_icc_node alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = { &qns_gemnoc_cnoc, &qns_llcc },
};

static struct qcom_icc_node chm_apps = {
	.name = "chm_apps",
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.link_nodes = { &qns_gemnoc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_gemnoc_cfg = {
	.name = "qnm_gemnoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &srvc_gemnoc },
};

static struct qcom_icc_node qnm_mdsp = {
	.name = "qnm_mdsp",
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.link_nodes = { &qns_gemnoc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_pcie = {
	.name = "qnm_pcie",
	.channels = 1,
	.buswidth = 16,
	.num_links = 2,
	.link_nodes = { &qns_gemnoc_cnoc, &qns_llcc },
};

static struct qcom_icc_node qnm_snoc_sf = {
	.name = "qnm_snoc_sf",
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.link_nodes = { &qns_gemnoc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node xm_gic = {
	.name = "xm_gic",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_llcc },
};

static struct qcom_icc_node xm_ipa2pcie = {
	.name = "xm_ipa2pcie",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_pcie },
};

static struct qcom_icc_node llcc_mc = {
	.name = "llcc_mc",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &ebi },
};

static struct qcom_icc_node xm_pcie3_0 = {
	.name = "xm_pcie3_0",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_pcie_gemnoc },
};

static struct qcom_icc_node xm_pcie3_1 = {
	.name = "xm_pcie3_1",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_pcie_gemnoc },
};

static struct qcom_icc_node xm_pcie3_2 = {
	.name = "xm_pcie3_2",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_pcie_gemnoc },
};

static struct qcom_icc_node qhm_audio = {
	.name = "qhm_audio",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_node qhm_gic = {
	.name = "qhm_gic",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_node qhm_pcie_rscc = {
	.name = "qhm_pcie_rscc",
	.channels = 1,
	.buswidth = 4,
	.num_links = 31,
	.link_nodes = { &ps_eth0_cfg, &ps_eth1_cfg,
			&qhs_audio, &qhs_clk_ctl,
			&qhs_crypto_cfg, &qhs_imem_cfg,
			&qhs_ipa, &qhs_ipc_router,
			&qhs_mss_cfg, &qhs_mvmss_cfg,
			&qhs_pcie0_cfg, &qhs_pcie1_cfg,
			&qhs_pcie2_cfg, &qhs_pdm,
			&qhs_prng, &qhs_qdss_cfg,
			&qhs_qpic, &qhs_qup0,
			&qhs_sdc1, &qhs_sdc4,
			&qhs_spmi_vgi_coex, &qhs_tcsr,
			&qhs_tlmm, &qhs_usb3,
			&qhs_usb3_phy, &qns_ddrss_cfg,
			&qns_system_noc_cfg, &qns_system_noc_pcie_cfg,
			&qxs_imem, &xs_qdss_stm,
			&xs_sys_tcu_cfg },
};

static struct qcom_icc_node qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qns_a1noc },
};

static struct qcom_icc_node qhm_qpic = {
	.name = "qhm_qpic",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qns_a1noc },
};

static struct qcom_icc_node qhm_qup0 = {
	.name = "qhm_qup0",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qns_a1noc },
};

static struct qcom_icc_node qnm_aggre_noc = {
	.name = "qnm_aggre_noc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_node qnm_gemnoc_cnoc = {
	.name = "qnm_gemnoc_cnoc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 32,
	.link_nodes = { &ps_eth0_cfg, &ps_eth1_cfg,
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
			&qns_ddrss_cfg, &qns_system_noc_cfg,
			&qns_system_noc_pcie_cfg, &qxs_imem,
			&xs_qdss_stm, &xs_sys_tcu_cfg },
};

static struct qcom_icc_node qnm_gemnoc_pcie = {
	.name = "qnm_gemnoc_pcie",
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.link_nodes = { &xs_pcie_0, &xs_pcie_1,
			&xs_pcie_2 },
};

static struct qcom_icc_node qnm_system_noc_cfg = {
	.name = "qnm_system_noc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &srvc_system_noc },
};

static struct qcom_icc_node qnm_system_noc_pcie_cfg = {
	.name = "qnm_system_noc_pcie_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &srvc_pcie_system_noc },
};

static struct qcom_icc_node qxm_crypto = {
	.name = "qxm_crypto",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_a1noc },
};

static struct qcom_icc_node qxm_ipa = {
	.name = "qxm_ipa",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_node qxm_mvmss = {
	.name = "qxm_mvmss",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_a1noc },
};

static struct qcom_icc_node xm_emac_0 = {
	.name = "xm_emac_0",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_a1noc },
};

static struct qcom_icc_node xm_emac_1 = {
	.name = "xm_emac_1",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_a1noc },
};

static struct qcom_icc_node xm_qdss_etr0 = {
	.name = "xm_qdss_etr0",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_a1noc },
};

static struct qcom_icc_node xm_qdss_etr1 = {
	.name = "xm_qdss_etr1",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_a1noc },
};

static struct qcom_icc_node xm_sdc1 = {
	.name = "xm_sdc1",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_a1noc },
};

static struct qcom_icc_node xm_sdc4 = {
	.name = "xm_sdc4",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_a1noc },
};

static struct qcom_icc_node xm_usb3 = {
	.name = "xm_usb3",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_a1noc },
};

static struct qcom_icc_node qup0_core_slave = {
	.name = "qup0_core_slave",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_lagg = {
	.name = "qhs_lagg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_mccc_master = {
	.name = "qhs_mccc_master",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_gemnoc = {
	.name = "qns_gemnoc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qss_snoop_bwmon = {
	.name = "qss_snoop_bwmon",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_gemnoc_cnoc = {
	.name = "qns_gemnoc_cnoc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qnm_gemnoc_cnoc },
};

static struct qcom_icc_node qns_llcc = {
	.name = "qns_llcc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &llcc_mc },
};

static struct qcom_icc_node qns_pcie = {
	.name = "qns_pcie",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_gemnoc_pcie },
};

static struct qcom_icc_node srvc_gemnoc = {
	.name = "srvc_gemnoc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_pcie_gemnoc = {
	.name = "qns_pcie_gemnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_pcie },
};

static struct qcom_icc_node ps_eth0_cfg = {
	.name = "ps_eth0_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node ps_eth1_cfg = {
	.name = "ps_eth1_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_audio = {
	.name = "qhs_audio",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_crypto_cfg = {
	.name = "qhs_crypto_cfg",
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

static struct qcom_icc_node qhs_ipc_router = {
	.name = "qhs_ipc_router",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_mss_cfg = {
	.name = "qhs_mss_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_mvmss_cfg = {
	.name = "qhs_mvmss_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie0_cfg = {
	.name = "qhs_pcie0_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie1_cfg = {
	.name = "qhs_pcie1_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie2_cfg = {
	.name = "qhs_pcie2_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie_rscc = {
	.name = "qhs_pcie_rscc",
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

static struct qcom_icc_node qhs_qup0 = {
	.name = "qhs_qup0",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_sdc1 = {
	.name = "qhs_sdc1",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_sdc4 = {
	.name = "qhs_sdc4",
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

static struct qcom_icc_node qns_a1noc = {
	.name = "qns_a1noc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qnm_aggre_noc },
};

static struct qcom_icc_node qns_ddrss_cfg = {
	.name = "qns_ddrss_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qnm_cnoc },
};

static struct qcom_icc_node qns_gemnoc_sf = {
	.name = "qns_gemnoc_sf",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_snoc_sf },
};

static struct qcom_icc_node qns_system_noc_cfg = {
	.name = "qns_system_noc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qnm_system_noc_cfg },
};

static struct qcom_icc_node qns_system_noc_pcie_cfg = {
	.name = "qns_system_noc_pcie_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qnm_system_noc_pcie_cfg },
};

static struct qcom_icc_node qxs_imem = {
	.name = "qxs_imem",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node srvc_pcie_system_noc = {
	.name = "srvc_pcie_system_noc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_system_noc = {
	.name = "srvc_system_noc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node xs_pcie_0 = {
	.name = "xs_pcie_0",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node xs_pcie_1 = {
	.name = "xs_pcie_1",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node xs_pcie_2 = {
	.name = "xs_pcie_2",
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
	.num_nodes = 1,
	.nodes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.keepalive = true,
	.num_nodes = 39,
	.nodes = { &qhm_pcie_rscc, &qnm_gemnoc_cnoc,
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
		   &qns_ddrss_cfg, &qns_system_noc_cfg,
		   &qns_system_noc_pcie_cfg, &qxs_imem,
		   &srvc_pcie_system_noc, &srvc_system_noc,
		   &xs_pcie_0, &xs_pcie_1,
		   &xs_pcie_2, &xs_qdss_stm,
		   &xs_sys_tcu_cfg },
};

static struct qcom_icc_bcm bcm_mc0 = {
	.name = "MC0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.keepalive = true,
	.vote_scale = 1,
	.num_nodes = 1,
	.nodes = { &qup0_core_slave },
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_sh1 = {
	.name = "SH1",
	.num_nodes = 10,
	.nodes = { &alm_sys_tcu, &chm_apps,
		   &qnm_gemnoc_cfg, &qnm_mdsp,
		   &qnm_snoc_sf, &xm_gic,
		   &xm_ipa2pcie, &qns_gemnoc_cnoc,
		   &qns_pcie, &srvc_gemnoc },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.num_nodes = 21,
	.nodes = { &xm_pcie3_0, &xm_pcie3_1,
		   &xm_pcie3_2, &qhm_audio,
		   &qhm_gic, &qhm_qdss_bam,
		   &qhm_qpic, &qhm_qup0,
		   &qnm_gemnoc_pcie, &qnm_system_noc_cfg,
		   &qnm_system_noc_pcie_cfg, &qxm_crypto,
		   &qxm_ipa, &qxm_mvmss,
		   &xm_emac_0, &xm_emac_1,
		   &xm_qdss_etr0, &xm_qdss_etr1,
		   &xm_sdc1, &xm_sdc4,
		   &xm_usb3 },
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.num_nodes = 2,
	.nodes = { &qnm_aggre_noc, &qns_a1noc },
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.num_nodes = 2,
	.nodes = { &qnm_pcie, &qns_pcie_gemnoc },
};

static struct qcom_icc_bcm * const clk_virt_bcms[] = {
	&bcm_qup0,
};

static struct qcom_icc_node * const clk_virt_nodes[] = {
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
};

static const struct qcom_icc_desc sdx75_clk_virt = {
	.nodes = clk_virt_nodes,
	.num_nodes = ARRAY_SIZE(clk_virt_nodes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
};

static struct qcom_icc_node * const dc_noc_nodes[] = {
	[MASTER_CNOC_DC_NOC] = &qnm_cnoc,
	[SLAVE_LAGG_CFG] = &qhs_lagg,
	[SLAVE_MCCC_MASTER] = &qhs_mccc_master,
	[SLAVE_GEM_NOC_CFG] = &qns_gemnoc,
	[SLAVE_SNOOP_BWMON] = &qss_snoop_bwmon,
};

static const struct qcom_icc_desc sdx75_dc_noc = {
	.nodes = dc_noc_nodes,
	.num_nodes = ARRAY_SIZE(dc_noc_nodes),
};

static struct qcom_icc_bcm * const gem_noc_bcms[] = {
	&bcm_sh0,
	&bcm_sh1,
	&bcm_sn4,
};

static struct qcom_icc_node * const gem_noc_nodes[] = {
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_APPSS_PROC] = &chm_apps,
	[MASTER_GEM_NOC_CFG] = &qnm_gemnoc_cfg,
	[MASTER_MSS_PROC] = &qnm_mdsp,
	[MASTER_ANOC_PCIE_GEM_NOC] = &qnm_pcie,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[MASTER_GIC] = &xm_gic,
	[MASTER_IPA_PCIE] = &xm_ipa2pcie,
	[SLAVE_GEM_NOC_CNOC] = &qns_gemnoc_cnoc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_MEM_NOC_PCIE_SNOC] = &qns_pcie,
	[SLAVE_SERVICE_GEM_NOC] = &srvc_gemnoc,
};

static const struct qcom_icc_desc sdx75_gem_noc = {
	.nodes = gem_noc_nodes,
	.num_nodes = ARRAY_SIZE(gem_noc_nodes),
	.bcms = gem_noc_bcms,
	.num_bcms = ARRAY_SIZE(gem_noc_bcms),
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_mc0,
};

static struct qcom_icc_node * const mc_virt_nodes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
};

static const struct qcom_icc_desc sdx75_mc_virt = {
	.nodes = mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm * const pcie_anoc_bcms[] = {
	&bcm_sn1,
	&bcm_sn4,
};

static struct qcom_icc_node * const pcie_anoc_nodes[] = {
	[MASTER_PCIE_0] = &xm_pcie3_0,
	[MASTER_PCIE_1] = &xm_pcie3_1,
	[MASTER_PCIE_2] = &xm_pcie3_2,
	[SLAVE_ANOC_PCIE_GEM_NOC] = &qns_pcie_gemnoc,
};

static const struct qcom_icc_desc sdx75_pcie_anoc = {
	.nodes = pcie_anoc_nodes,
	.num_nodes = ARRAY_SIZE(pcie_anoc_nodes),
	.bcms = pcie_anoc_bcms,
	.num_bcms = ARRAY_SIZE(pcie_anoc_bcms),
};

static struct qcom_icc_bcm * const system_noc_bcms[] = {
	&bcm_ce0,
	&bcm_cn0,
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn2,
};

static struct qcom_icc_node * const system_noc_nodes[] = {
	[MASTER_AUDIO] = &qhm_audio,
	[MASTER_GIC_AHB] = &qhm_gic,
	[MASTER_PCIE_RSCC] = &qhm_pcie_rscc,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QPIC] = &qhm_qpic,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_ANOC_SNOC] = &qnm_aggre_noc,
	[MASTER_GEM_NOC_CNOC] = &qnm_gemnoc_cnoc,
	[MASTER_GEM_NOC_PCIE_SNOC] = &qnm_gemnoc_pcie,
	[MASTER_SNOC_CFG] = &qnm_system_noc_cfg,
	[MASTER_PCIE_ANOC_CFG] = &qnm_system_noc_pcie_cfg,
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
	[SLAVE_CNOC_MSS] = &qhs_mss_cfg,
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
	[SLAVE_A1NOC_CFG] = &qns_a1noc,
	[SLAVE_DDRSS_CFG] = &qns_ddrss_cfg,
	[SLAVE_SNOC_GEM_NOC_SF] = &qns_gemnoc_sf,
	[SLAVE_SNOC_CFG] = &qns_system_noc_cfg,
	[SLAVE_PCIE_ANOC_CFG] = &qns_system_noc_pcie_cfg,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_SERVICE_PCIE_ANOC] = &srvc_pcie_system_noc,
	[SLAVE_SERVICE_SNOC] = &srvc_system_noc,
	[SLAVE_PCIE_0] = &xs_pcie_0,
	[SLAVE_PCIE_1] = &xs_pcie_1,
	[SLAVE_PCIE_2] = &xs_pcie_2,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sdx75_system_noc = {
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,sdx75-clk-virt", .data = &sdx75_clk_virt },
	{ .compatible = "qcom,sdx75-dc-noc", .data = &sdx75_dc_noc },
	{ .compatible = "qcom,sdx75-gem-noc", .data = &sdx75_gem_noc },
	{ .compatible = "qcom,sdx75-mc-virt", .data = &sdx75_mc_virt },
	{ .compatible = "qcom,sdx75-pcie-anoc", .data = &sdx75_pcie_anoc },
	{ .compatible = "qcom,sdx75-system-noc", .data = &sdx75_system_noc },
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-sdx75",
		.of_match_table = qnoc_of_match,
		.sync_state = icc_sync_state,
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

MODULE_DESCRIPTION("SDX75 NoC driver");
MODULE_LICENSE("GPL");
