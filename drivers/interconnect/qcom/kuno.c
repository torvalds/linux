// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/interconnect/qcom,kuno.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"

static struct qcom_icc_node qup0_core_slave = {
	.name = "qup0_core_slave",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ahb2_phy = {
	.name = "qhs_ahb2_phy",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_aoss = {
	.name = "qhs_aoss",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_apss = {
	.name = "qhs_apss",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_audio = {
	.name = "qhs_audio",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_cpr_mxa = {
	.name = "qhs_cpr_mxa",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_cpr_mxc = {
	.name = "qhs_cpr_mxc",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_crypto_cfg = {
	.name = "qhs_crypto_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_emac0_cfg = {
	.name = "qhs_emac0_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ipa = {
	.name = "qhs_ipa",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ipc_router = {
	.name = "qhs_ipc_router",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_mss_cfg = {
	.name = "qhs_mss_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pcie0_cfg = {
	.name = "qhs_pcie0_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pcie_rscc = {
	.name = "qhs_pcie_rscc",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pdm = {
	.name = "qhs_pdm",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pmu_wrapper_cfg = {
	.name = "qhs_pmu_wrapper_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_prng = {
	.name = "qhs_prng",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qpic = {
	.name = "qhs_qpic",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qup0 = {
	.name = "qhs_qup0",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_sdc4 = {
	.name = "qhs_sdc4",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_spmi_vgi_coex = {
	.name = "qhs_spmi_vgi_coex",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_tcsr = {
	.name = "qhs_tcsr",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_tlmm = {
	.name = "qhs_tlmm",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_tme_cfg = {
	.name = "qhs_tme_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_usb3 = {
	.name = "qhs_usb3",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qss_anoc_throttle_cfg = {
	.name = "qss_anoc_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qxs_boot_imem = {
	.name = "qxs_boot_imem",
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node qxs_imem = {
	.name = "qxs_imem",
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct qcom_icc_node qhs_lagg = {
	.name = "qhs_lagg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node qhs_mccc_master = {
	.name = "qhs_mccc_master",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node srvc_memnoc = {
	.name = "srvc_memnoc",
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct qcom_icc_node xs_pcie_0 = {
	.name = "xs_pcie_0",
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

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
	.num_links = 2,
	.link_nodes = { &qhs_lagg, &qhs_mccc_master },
};

static struct qcom_icc_node llcc_mc = {
	.name = "llcc_mc",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &ebi },
};

static struct qcom_icc_node qnm_memnoc_cfg = {
	.name = "qnm_memnoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &srvc_memnoc },
};

static struct qcom_icc_node qnm_memnoc_pcie = {
	.name = "qnm_memnoc_pcie",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &xs_pcie_0 },
};

static struct qcom_icc_node xm_ipa2pcie = {
	.name = "xm_ipa2pcie",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &xs_pcie_0 },
};

static struct qcom_icc_node qns_ddrss_cfg = {
	.name = "qns_ddrss_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qnm_cnoc },
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
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qnm_memnoc_pcie },
};

static struct qcom_icc_node qhm_pcie_rscc = {
	.name = "qhm_pcie_rscc",
	.channels = 1,
	.buswidth = 4,
	.num_links = 34,
	.link_nodes = { &qhs_ahb2_phy, &qhs_aoss,
			&qhs_apss, &qhs_audio,
			&qhs_clk_ctl, &qhs_cpr_cx,
			&qhs_cpr_mxa, &qhs_cpr_mxc,
			&qhs_crypto_cfg, &qhs_emac0_cfg,
			&qhs_imem_cfg, &qhs_ipa,
			&qhs_ipc_router, &qhs_mss_cfg,
			&qhs_pcie0_cfg, &qhs_pdm,
			&qhs_pmu_wrapper_cfg, &qhs_prng,
			&qhs_qdss_cfg, &qhs_qpic,
			&qhs_qup0, &qhs_sdc4,
			&qhs_spmi_vgi_coex, &qhs_tcsr,
			&qhs_tlmm, &qhs_tme_cfg,
			&qhs_usb3, &qhs_vsense_ctrl_cfg,
			&qns_ddrss_cfg, &qss_anoc_throttle_cfg,
			&qxs_boot_imem, &qxs_imem,
			&xs_qdss_stm, &xs_sys_tcu_cfg },
};

static struct qcom_icc_node qnm_memnoc_cnoc = {
	.name = "qnm_memnoc_cnoc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 35,
	.link_nodes = { &qhs_ahb2_phy, &qhs_aoss,
			&qhs_apss, &qhs_audio,
			&qhs_clk_ctl, &qhs_cpr_cx,
			&qhs_cpr_mxa, &qhs_cpr_mxc,
			&qhs_crypto_cfg, &qhs_emac0_cfg,
			&qhs_imem_cfg, &qhs_ipa,
			&qhs_ipc_router, &qhs_mss_cfg,
			&qhs_pcie0_cfg, &qhs_pcie_rscc,
			&qhs_pdm, &qhs_pmu_wrapper_cfg,
			&qhs_prng, &qhs_qdss_cfg,
			&qhs_qpic, &qhs_qup0,
			&qhs_sdc4, &qhs_spmi_vgi_coex,
			&qhs_tcsr, &qhs_tlmm,
			&qhs_tme_cfg, &qhs_usb3,
			&qhs_vsense_ctrl_cfg, &qns_ddrss_cfg,
			&qss_anoc_throttle_cfg, &qxs_boot_imem,
			&qxs_imem, &xs_qdss_stm,
			&xs_sys_tcu_cfg },
};

static struct qcom_icc_node qns_memnoc_cnoc = {
	.name = "qns_memnoc_cnoc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qnm_memnoc_cnoc },
};

static struct qcom_icc_node alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = { &qns_llcc, &qns_memnoc_cnoc },
};

static struct qcom_icc_node qnm_mdsp = {
	.name = "qnm_mdsp",
	.channels = 1,
	.buswidth = 8,
	.num_links = 3,
	.link_nodes = { &qns_llcc, &qns_memnoc_cnoc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_pcie = {
	.name = "qnm_pcie",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = { &qns_llcc, &qns_memnoc_cnoc },
};

static struct qcom_icc_node qnm_snoc_sf = {
	.name = "qnm_snoc_sf",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = { &qns_llcc, &qns_memnoc_cnoc },
};

static struct qcom_icc_node xm_apps0 = {
	.name = "xm_apps0",
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.link_nodes = { &qns_llcc, &qns_memnoc_cnoc,
			&qns_pcie },
};

static struct qcom_icc_node qns_pcie_memnoc = {
	.name = "qns_pcie_memnoc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qnm_pcie },
};

static struct qcom_icc_node qns_memnoc_sf = {
	.name = "qns_memnoc_sf",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qnm_snoc_sf },
};

static struct qcom_icc_node xm_pcie3_0 = {
	.name = "xm_pcie3_0",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_pcie_memnoc },
};

static struct qcom_icc_node qhm_cpucp = {
	.name = "qhm_cpucp",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qns_memnoc_sf },
};

static struct qcom_icc_node qnm_aggre_noc = {
	.name = "qnm_aggre_noc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = { &qns_memnoc_sf, &xs_pcie_0 },
};

static struct qcom_icc_node qnm_cnoc_datapath = {
	.name = "qnm_cnoc_datapath",
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.link_nodes = { &qns_memnoc_sf, &xs_pcie_0 },
};

static struct qcom_icc_node qxm_mss_nav_ce = {
	.name = "qxm_mss_nav_ce",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = { &qns_memnoc_sf, &xs_pcie_0 },
};

static struct qcom_icc_node qxm_tme = {
	.name = "qxm_tme",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = { &qns_memnoc_sf, &xs_pcie_0 },
};

static struct qcom_icc_node qns_a1noc = {
	.name = "qns_a1noc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qnm_aggre_noc },
};

static struct qcom_icc_node qns_snoc_datapath = {
	.name = "qns_snoc_datapath",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qnm_cnoc_datapath },
};

static struct qcom_icc_node qhm_audio = {
	.name = "qhm_audio",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qns_a1noc },
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
	.link_nodes = { &qns_a1noc },
};

static struct qcom_icc_node xm_emac_0 = {
	.name = "xm_emac_0",
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

static struct qcom_icc_node xm_qdss_dap = {
	.name = "xm_qdss_dap",
	.channels = 1,
	.buswidth = 8,
	.num_links = 36,
	.link_nodes = { &qhs_ahb2_phy, &qhs_aoss,
			&qhs_apss, &qhs_audio,
			&qhs_clk_ctl, &qhs_cpr_cx,
			&qhs_cpr_mxa, &qhs_cpr_mxc,
			&qhs_crypto_cfg, &qhs_emac0_cfg,
			&qhs_imem_cfg, &qhs_ipa,
			&qhs_ipc_router, &qhs_mss_cfg,
			&qhs_pcie0_cfg, &qhs_pcie_rscc,
			&qhs_pdm, &qhs_pmu_wrapper_cfg,
			&qhs_prng, &qhs_qdss_cfg,
			&qhs_qpic, &qhs_qup0,
			&qhs_sdc4, &qhs_spmi_vgi_coex,
			&qhs_tcsr, &qhs_tlmm,
			&qhs_tme_cfg, &qhs_usb3,
			&qhs_vsense_ctrl_cfg, &qns_ddrss_cfg,
			&qns_snoc_datapath, &qss_anoc_throttle_cfg,
			&qxs_boot_imem, &qxs_imem,
			&xs_qdss_stm, &xs_sys_tcu_cfg },
};

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
	.enable_mask = 0x8,
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
	.keepalive = true,
	.enable_mask = 0x1,
	.num_nodes = 39,
	.nodes = { &qhm_pcie_rscc, &qnm_memnoc_cnoc,
		   &xm_qdss_dap, &qhs_ahb2_phy,
		   &qhs_aoss, &qhs_apss,
		   &qhs_audio, &qhs_clk_ctl,
		   &qhs_cpr_cx, &qhs_cpr_mxa,
		   &qhs_cpr_mxc, &qhs_crypto_cfg,
		   &qhs_emac0_cfg, &qhs_imem_cfg,
		   &qhs_ipa, &qhs_ipc_router,
		   &qhs_mss_cfg, &qhs_pcie0_cfg,
		   &qhs_pcie_rscc, &qhs_pdm,
		   &qhs_pmu_wrapper_cfg, &qhs_prng,
		   &qhs_qdss_cfg, &qhs_qpic,
		   &qhs_qup0, &qhs_sdc4,
		   &qhs_spmi_vgi_coex, &qhs_tcsr,
		   &qhs_tlmm, &qhs_tme_cfg,
		   &qhs_usb3, &qhs_vsense_ctrl_cfg,
		   &qns_ddrss_cfg, &qns_snoc_datapath,
		   &qss_anoc_throttle_cfg, &qxs_boot_imem,
		   &qxs_imem, &xs_qdss_stm,
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
	.enable_mask = 0x1,
	.num_nodes = 2,
	.nodes = { &qns_memnoc_cnoc, &qns_pcie },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_memnoc_sf },
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.enable_mask = 0x1,
	.num_nodes = 1,
	.nodes = { &xm_pcie3_0 },
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.num_nodes = 2,
	.nodes = { &qns_a1noc, &qnm_aggre_noc },
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.num_nodes = 1,
	.nodes = { &qxm_mss_nav_ce },
};

static struct qcom_icc_bcm bcm_sn7 = {
	.name = "SN7",
	.num_nodes = 1,
	.nodes = { &xs_pcie_0 },
};

static struct qcom_icc_bcm * const aggre_noc_bcms[] = {
	&bcm_ce0,
};

static struct qcom_icc_node * const aggre_noc_nodes[] = {
	[MASTER_AUDIO] = &qhm_audio,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QPIC] = &qhm_qpic,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_EMAC] = &xm_emac_0,
	[MASTER_QDSS_ETR] = &xm_qdss_etr0,
	[MASTER_QDSS_ETR_1] = &xm_qdss_etr1,
	[MASTER_SDCC_4] = &xm_sdc4,
	[MASTER_USB3_0] = &xm_usb3,
	[SLAVE_A1NOC_CFG] = &qns_a1noc,
};

static const struct qcom_icc_desc kuno_aggre_noc = {
	.nodes = aggre_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre_noc_nodes),
	.bcms = aggre_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre_noc_bcms),
};

static struct qcom_icc_bcm * const clk_virt_bcms[] = {
	&bcm_qup0,
};

static struct qcom_icc_node * const clk_virt_nodes[] = {
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
};

static const struct qcom_icc_desc kuno_clk_virt = {
	.nodes = clk_virt_nodes,
	.num_nodes = ARRAY_SIZE(clk_virt_nodes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
};

static struct qcom_icc_bcm * const cnoc_main_bcms[] = {
	&bcm_cn0,
};

static struct qcom_icc_node * const cnoc_main_nodes[] = {
	[MASTER_PCIE_RSCC] = &qhm_pcie_rscc,
	[MASTER_GEM_NOC_CNOC] = &qnm_memnoc_cnoc,
	[MASTER_QDSS_DAP] = &xm_qdss_dap,
	[SLAVE_AHB2PHY] = &qhs_ahb2_phy,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_APPSS] = &qhs_apss,
	[SLAVE_AUDIO] = &qhs_audio,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MXA_CFG] = &qhs_cpr_mxa,
	[SLAVE_RBCPR_MXC_CFG] = &qhs_cpr_mxc,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto_cfg,
	[SLAVE_EMAC_CFG] = &qhs_emac0_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_IPC_ROUTER_CFG] = &qhs_ipc_router,
	[SLAVE_CNOC_MSS] = &qhs_mss_cfg,
	[SLAVE_PCIE_0_CFG] = &qhs_pcie0_cfg,
	[SLAVE_PCIE_RSC_CFG] = &qhs_pcie_rscc,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PMU_WRAPPER_CFG] = &qhs_pmu_wrapper_cfg,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QPIC] = &qhs_qpic,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_SDCC_4] = &qhs_sdc4,
	[SLAVE_SPMI_VGI_COEX] = &qhs_spmi_vgi_coex,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_TME_CFG] = &qhs_tme_cfg,
	[SLAVE_USB3] = &qhs_usb3,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_DDRSS_CFG] = &qns_ddrss_cfg,
	[SLAVE_SNOC_CNOC] = &qns_snoc_datapath,
	[SLAVE_ANOC_THROTTLE_CFG] = &qss_anoc_throttle_cfg,
	[SLAVE_BOOT_IMEM] = &qxs_boot_imem,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc kuno_cnoc_main = {
	.nodes = cnoc_main_nodes,
	.num_nodes = ARRAY_SIZE(cnoc_main_nodes),
	.bcms = cnoc_main_bcms,
	.num_bcms = ARRAY_SIZE(cnoc_main_bcms),
};

static struct qcom_icc_node * const dc_noc_nodes[] = {
	[MASTER_CNOC_DC_NOC] = &qnm_cnoc,
	[SLAVE_LAGG_CFG] = &qhs_lagg,
	[SLAVE_MCCC_MASTER] = &qhs_mccc_master,
};

static const struct qcom_icc_desc kuno_dc_noc = {
	.nodes = dc_noc_nodes,
	.num_nodes = ARRAY_SIZE(dc_noc_nodes),
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
};

static struct qcom_icc_node * const mc_virt_nodes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
};

static const struct qcom_icc_desc kuno_mc_virt = {
	.nodes = mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm * const mem_noc_bcms[] = {
	&bcm_sh0,
	&bcm_sh1,
};

static struct qcom_icc_node * const mem_noc_nodes[] = {
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_MSS_PROC] = &qnm_mdsp,
	[MASTER_GEM_NOC_CFG] = &qnm_memnoc_cfg,
	[MASTER_ANOC_PCIE_GEM_NOC] = &qnm_pcie,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[MASTER_APPSS_PROC] = &xm_apps0,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_GEM_NOC_CNOC] = &qns_memnoc_cnoc,
	[SLAVE_MEM_NOC_PCIE_SNOC] = &qns_pcie,
	[SLAVE_SERVICE_GEM_NOC] = &srvc_memnoc,
};

static const struct qcom_icc_desc kuno_mem_noc = {
	.nodes = mem_noc_nodes,
	.num_nodes = ARRAY_SIZE(mem_noc_nodes),
	.bcms = mem_noc_bcms,
	.num_bcms = ARRAY_SIZE(mem_noc_bcms),
};

static struct qcom_icc_bcm * const pcie_anoc_bcms[] = {
	&bcm_sn1,
};

static struct qcom_icc_node * const pcie_anoc_nodes[] = {
	[MASTER_PCIE_0] = &xm_pcie3_0,
	[SLAVE_ANOC_PCIE_GEM_NOC] = &qns_pcie_memnoc,
};

static const struct qcom_icc_desc kuno_pcie_anoc = {
	.nodes = pcie_anoc_nodes,
	.num_nodes = ARRAY_SIZE(pcie_anoc_nodes),
	.bcms = pcie_anoc_bcms,
	.num_bcms = ARRAY_SIZE(pcie_anoc_bcms),
};

static struct qcom_icc_bcm * const system_noc_bcms[] = {
	&bcm_sn0,
	&bcm_sn2,
	&bcm_sn4,
	&bcm_sn7,
};

static struct qcom_icc_node * const system_noc_nodes[] = {
	[MASTER_CPUCP] = &qhm_cpucp,
	[MASTER_ANOC_SNOC] = &qnm_aggre_noc,
	[MASTER_CNOC_SNOC] = &qnm_cnoc_datapath,
	[MASTER_GEM_NOC_PCIE_SNOC] = &qnm_memnoc_pcie,
	[MASTER_MSS_NAV] = &qxm_mss_nav_ce,
	[MASTER_TME] = &qxm_tme,
	[MASTER_IPA_PCIE] = &xm_ipa2pcie,
	[SLAVE_SNOC_MEM_NOC_SF] = &qns_memnoc_sf,
	[SLAVE_PCIE_0] = &xs_pcie_0,
};

static const struct qcom_icc_desc kuno_system_noc = {
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,kuno-aggre-noc", .data = &kuno_aggre_noc },
	{ .compatible = "qcom,kuno-clk-virt", .data = &kuno_clk_virt },
	{ .compatible = "qcom,kuno-cnoc-main", .data = &kuno_cnoc_main },
	{ .compatible = "qcom,kuno-dc-noc", .data = &kuno_dc_noc },
	{ .compatible = "qcom,kuno-mc-virt", .data = &kuno_mc_virt },
	{ .compatible = "qcom,kuno-mem-noc", .data = &kuno_mem_noc },
	{ .compatible = "qcom,kuno-pcie-anoc", .data = &kuno_pcie_anoc },
	{ .compatible = "qcom,kuno-system-noc", .data = &kuno_system_noc },
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-kuno",
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

MODULE_DESCRIPTION("Kuno NoC driver");
MODULE_LICENSE("GPL");
