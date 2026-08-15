// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <dt-bindings/interconnect/qcom,nord-rpmh.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"

static struct qcom_icc_node qup0_core_slave = {
	.name = "qup0_core_slave",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qup1_core_slave = {
	.name = "qup1_core_slave",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qup2_core_slave = {
	.name = "qup2_core_slave",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qup3_core_slave = {
	.name = "qup3_core_slave",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node ps_eth_0 = {
	.name = "ps_eth_0",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node ps_eth_1 = {
	.name = "ps_eth_1",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node ps_shs_server = {
	.name = "ps_shs_server",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ahb2phy0 = {
	.name = "qhs_ahb2phy0",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ahb2phy1 = {
	.name = "qhs_ahb2phy1",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ahb2phy2 = {
	.name = "qhs_ahb2phy2",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ahb2phy3 = {
	.name = "qhs_ahb2phy3",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ahb2phy_eth_0 = {
	.name = "qhs_ahb2phy_eth_0",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ahb2phy_eth_1 = {
	.name = "qhs_ahb2phy_eth_1",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_camera_cfg = {
	.name = "qhs_camera_cfg",
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

static struct qcom_icc_node qhs_crypto1_cfg = {
	.name = "qhs_crypto1_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_crypto2_cfg = {
	.name = "qhs_crypto2_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_display_1_cfg = {
	.name = "qhs_display_1_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_display_cfg = {
	.name = "qhs_display_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_dprx0 = {
	.name = "qhs_dprx0",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_dprx1 = {
	.name = "qhs_dprx1",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_eva_cfg = {
	.name = "qhs_eva_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_gpuss_0_cfg = {
	.name = "qhs_gpuss_0_cfg",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qhs_gpuss_1_cfg = {
	.name = "qhs_gpuss_1_cfg",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qhs_i2c = {
	.name = "qhs_i2c",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_mcw_pcie = {
	.name = "qhs_mcw_pcie",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_mm_rscc = {
	.name = "qhs_mm_rscc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ne_clk_ctl = {
	.name = "qhs_ne_clk_ctl",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_nspss0_cfg = {
	.name = "qhs_nspss0_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_nspss1_cfg = {
	.name = "qhs_nspss1_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_nspss2_cfg = {
	.name = "qhs_nspss2_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_nspss3_cfg = {
	.name = "qhs_nspss3_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_nw_clk_ctl = {
	.name = "qhs_nw_clk_ctl",
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

static struct qcom_icc_node qhs_qspi = {
	.name = "qhs_qspi",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qup0 = {
	.name = "qhs_qup0",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qup3 = {
	.name = "qhs_qup3",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qup1 = {
	.name = "qhs_qup1",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qup2 = {
	.name = "qhs_qup2",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_safedma_cfg = {
	.name = "qhs_safedma_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_sdc4 = {
	.name = "qhs_sdc4",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_se_clk_ctl = {
	.name = "qhs_se_clk_ctl",
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

static struct qcom_icc_node qhs_tsc_cfg = {
	.name = "qhs_tsc_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb2 = {
	.name = "qhs_usb2",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb3_0 = {
	.name = "qhs_usb3_0",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb3_1 = {
	.name = "qhs_usb3_1",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qss_computenoc_cfg = {
	.name = "qss_computenoc_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qss_qtc_cfg = {
	.name = "qss_qtc_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node xs_sys_tcu0_cfg = {
	.name = "xs_sys_tcu0_cfg",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node xs_sys_tcu1_cfg = {
	.name = "xs_sys_tcu1_cfg",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node xs_sys_tcu2_cfg = {
	.name = "xs_sys_tcu2_cfg",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qhs_aoss = {
	.name = "qhs_aoss",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_hbcu = {
	.name = "qhs_hbcu",
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

static struct qcom_icc_node qhs_soccp = {
	.name = "qhs_soccp",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_tme_cfg = {
	.name = "qhs_tme_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qss_ddrss_cfg = {
	.name = "qss_ddrss_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qxs_imem = {
	.name = "qxs_imem",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.channels = 16,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie_ahb2phy_cfg = {
	.name = "qhs_pcie_ahb2phy_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie_cfg_0 = {
	.name = "qhs_pcie_cfg_0",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie_cfg_1 = {
	.name = "qhs_pcie_cfg_1",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie_cfg_2 = {
	.name = "qhs_pcie_cfg_2",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie_cfg_3 = {
	.name = "qhs_pcie_cfg_3",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie_dma_0_cfg = {
	.name = "qhs_pcie_dma_0_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie_dma_1_cfg = {
	.name = "qhs_pcie_dma_1_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie_dma_2_cfg = {
	.name = "qhs_pcie_dma_2_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qxs_pcie_dma_0 = {
	.name = "qxs_pcie_dma_0",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qxs_pcie_dma_1 = {
	.name = "qxs_pcie_dma_1",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qxs_pcie_dma_2 = {
	.name = "qxs_pcie_dma_2",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node xs_pcie_0 = {
	.name = "xs_pcie_0",
	.channels = 1,
	.buswidth = 32,
};

static struct qcom_icc_node xs_pcie_1 = {
	.name = "xs_pcie_1",
	.channels = 1,
	.buswidth = 32,
};

static struct qcom_icc_node xs_pcie_2 = {
	.name = "xs_pcie_2",
	.channels = 1,
	.buswidth = 16,
};

static struct qcom_icc_node xs_pcie_3 = {
	.name = "xs_pcie_3",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qup0_core_master = {
	.name = "qup0_core_master",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qup0_core_slave },
};

static struct qcom_icc_node qup1_core_master = {
	.name = "qup1_core_master",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qup1_core_slave },
};

static struct qcom_icc_node qup2_core_master = {
	.name = "qup2_core_master",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qup2_core_slave },
};

static struct qcom_icc_node qup3_core_master = {
	.name = "qup3_core_master",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qup3_core_slave },
};

static struct qcom_icc_node llcc_mc = {
	.name = "llcc_mc",
	.channels = 16,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &ebi },
};

static struct qcom_icc_node qsm_pcie_noc_cfg = {
	.name = "qsm_pcie_noc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 8,
	.link_nodes = { &qhs_pcie_ahb2phy_cfg, &qhs_pcie_cfg_0,
			&qhs_pcie_cfg_1, &qhs_pcie_cfg_2,
			&qhs_pcie_cfg_3, &qhs_pcie_dma_0_cfg,
			&qhs_pcie_dma_1_cfg, &qhs_pcie_dma_2_cfg },
};

static struct qcom_icc_node qnm_cnoc_pcie_dma = {
	.name = "qnm_cnoc_pcie_dma",
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.link_nodes = { &qxs_pcie_dma_0, &qxs_pcie_dma_1,
			&qxs_pcie_dma_2 },
};

static struct qcom_icc_node qnm_hscnoc_pcie = {
	.name = "qnm_hscnoc_pcie",
	.channels = 1,
	.buswidth = 32,
	.num_links = 4,
	.link_nodes = { &xs_pcie_0, &xs_pcie_1,
			&xs_pcie_2, &xs_pcie_3 },
};

static struct qcom_icc_node qnm_pcie_ibnoc_dma = {
	.name = "qnm_pcie_ibnoc_dma",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &xs_pcie_0 },
};

static struct qcom_icc_node qss_pcie_noc_cfg = {
	.name = "qss_pcie_noc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qsm_pcie_noc_cfg },
};

static struct qcom_icc_node qns_pcie_dma = {
	.name = "qns_pcie_dma",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_cnoc_pcie_dma },
};

static struct qcom_icc_node qns_llcc = {
	.name = "qns_llcc",
	.channels = 16,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &llcc_mc },
};

static struct qcom_icc_node qns_pcie = {
	.name = "qns_pcie",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qnm_hscnoc_pcie },
};

static struct qcom_icc_node qns_pcie_obnoc_dma = {
	.name = "qns_pcie_obnoc_dma",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_pcie_ibnoc_dma },
};

static struct qcom_icc_node qsm_cfg = {
	.name = "qsm_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 56,
	.link_nodes = { &ps_eth_0, &ps_eth_1,
			&ps_shs_server, &qhs_ahb2phy0,
			&qhs_ahb2phy1, &qhs_ahb2phy2,
			&qhs_ahb2phy3, &qhs_ahb2phy_eth_0,
			&qhs_ahb2phy_eth_1, &qhs_camera_cfg,
			&qhs_clk_ctl, &qhs_crypto0_cfg,
			&qhs_crypto1_cfg, &qhs_crypto2_cfg,
			&qhs_display_1_cfg, &qhs_display_cfg,
			&qhs_dprx0, &qhs_dprx1,
			&qhs_eva_cfg, &qhs_gpuss_0_cfg,
			&qhs_gpuss_1_cfg, &qhs_i2c,
			&qhs_imem_cfg, &qhs_mcw_pcie,
			&qhs_mm_rscc, &qhs_ne_clk_ctl,
			&qhs_nspss0_cfg, &qhs_nspss1_cfg,
			&qhs_nspss2_cfg, &qhs_nspss3_cfg,
			&qhs_nw_clk_ctl, &qhs_prng,
			&qhs_qdss_cfg, &qhs_qspi,
			&qhs_qup0, &qhs_qup3,
			&qhs_qup1, &qhs_qup2,
			&qhs_safedma_cfg, &qhs_sdc4,
			&qhs_se_clk_ctl, &qhs_tcsr,
			&qhs_tlmm, &qhs_tsc_cfg,
			&qhs_ufs_mem_cfg, &qhs_usb2,
			&qhs_usb3_0, &qhs_usb3_1,
			&qhs_venus_cfg, &qss_computenoc_cfg,
			&qss_pcie_noc_cfg, &qss_qtc_cfg,
			&xs_qdss_stm, &xs_sys_tcu0_cfg,
			&xs_sys_tcu1_cfg, &xs_sys_tcu2_cfg },
};

static struct qcom_icc_node xm_gic = {
	.name = "xm_gic",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xa44000 },
		.prio = 4,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_llcc },
};

static struct qcom_icc_node qss_cfg = {
	.name = "qss_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qsm_cfg },
};

static struct qcom_icc_node qhm_mm_rscc = {
	.name = "qhm_mm_rscc",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qss_cfg },
};

static struct qcom_icc_node qnm_hscnoc = {
	.name = "qnm_hscnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 10,
	.link_nodes = { &qhs_aoss, &qhs_hbcu,
			&qhs_ipa, &qhs_ipc_router,
			&qhs_soccp, &qhs_tme_cfg,
			&qns_pcie_dma, &qss_cfg,
			&qss_ddrss_cfg, &qxs_imem },
};

static struct qcom_icc_node qns_hscnoc_cnoc = {
	.name = "qns_hscnoc_cnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_hscnoc },
};

static struct qcom_icc_node alm_gpu_tcu = {
	.name = "alm_gpu_tcu",
	.channels = 2,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x930000, 0xa45000 },
		.prio = 1,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 2,
	.link_nodes = { &qns_hscnoc_cnoc, &qns_llcc },
};

static struct qcom_icc_node alm_qtc = {
	.name = "alm_qtc",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x2c0000 },
		.prio = 3,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 2,
	.link_nodes = { &qns_hscnoc_cnoc, &qns_llcc },
};

static struct qcom_icc_node alm_sys_tcu0 = {
	.name = "alm_sys_tcu0",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xa42000 },
		.prio = 6,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 2,
	.link_nodes = { &qns_hscnoc_cnoc, &qns_llcc },
};

static struct qcom_icc_node alm_sys_tcu1 = {
	.name = "alm_sys_tcu1",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x81f000 },
		.prio = 6,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 2,
	.link_nodes = { &qns_hscnoc_cnoc, &qns_llcc },
};

static struct qcom_icc_node alm_sys_tcu2 = {
	.name = "alm_sys_tcu2",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x30000 },
		.prio = 6,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 2,
	.link_nodes = { &qns_hscnoc_cnoc, &qns_llcc },
};

static struct qcom_icc_node chm_apps = {
	.name = "chm_apps",
	.channels = 6,
	.buswidth = 32,
	.num_links = 3,
	.link_nodes = { &qns_hscnoc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_aggre_north = {
	.name = "qnm_aggre_north",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x935000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 3,
	.link_nodes = { &qns_hscnoc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_aggre_south = {
	.name = "qnm_aggre_south",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x31000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 3,
	.link_nodes = { &qns_hscnoc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_gpu0 = {
	.name = "qnm_gpu0",
	.channels = 4,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 4,
		.port_offsets = { 0x931000, 0x932000, 0x933000, 0x934000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 3,
	.link_nodes = { &qns_hscnoc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_gpu1 = {
	.name = "qnm_gpu1",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0xa40000, 0xa41000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 3,
	.link_nodes = { &qns_hscnoc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_hpass_adas_hscnoc = {
	.name = "qnm_hpass_adas_hscnoc",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x240000, 0x245000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 3,
	.link_nodes = { &qns_hscnoc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_hpass_audio_hscnoc = {
	.name = "qnm_hpass_audio_hscnoc",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x241000 },
		.prio = 3,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 3,
	.link_nodes = { &qns_hscnoc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_mnoc_hf = {
	.name = "qnm_mnoc_hf",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x81d000, 0x820000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 3,
	.link_nodes = { &qns_hscnoc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_mnoc_sf = {
	.name = "qnm_mnoc_sf",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x81e000, 0x821000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 3,
	.link_nodes = { &qns_hscnoc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_nsp0_hscnoc = {
	.name = "qnm_nsp0_hscnoc",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x32000, 0x33000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 3,
	.link_nodes = { &qns_hscnoc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_nsp1_hscnoc = {
	.name = "qnm_nsp1_hscnoc",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x34000, 0x35000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 3,
	.link_nodes = { &qns_hscnoc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_nsp2_hscnoc = {
	.name = "qnm_nsp2_hscnoc",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x36000, 0x37000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 3,
	.link_nodes = { &qns_hscnoc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_nsp3_hscnoc = {
	.name = "qnm_nsp3_hscnoc",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x38000, 0x39000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 3,
	.link_nodes = { &qns_hscnoc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_pcie = {
	.name = "qnm_pcie",
	.channels = 1,
	.buswidth = 64,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x2c1000 },
		.prio = 2,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 2,
	.link_nodes = { &qns_hscnoc_cnoc, &qns_llcc },
};

static struct qcom_icc_node qnm_sailss_md0_hscnoc = {
	.name = "qnm_sailss_md0_hscnoc",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x243000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 3,
	.link_nodes = { &qns_hscnoc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_snoc_sf = {
	.name = "qnm_snoc_sf",
	.channels = 1,
	.buswidth = 64,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xa43000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 3,
	.link_nodes = { &qns_hscnoc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qns_a1noc_hscnoc = {
	.name = "qns_a1noc_hscnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_aggre_north },
};

static struct qcom_icc_node qns_a2noc_hscnoc = {
	.name = "qns_a2noc_hscnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_aggre_south },
};

static struct qcom_icc_node qns_hpass_agnoc_audio = {
	.name = "qns_hpass_agnoc_audio",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qnm_hpass_audio_hscnoc },
};

static struct qcom_icc_node qns_mem_noc_hf = {
	.name = "qns_mem_noc_hf",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qnm_mnoc_hf },
};

static struct qcom_icc_node qns_mem_noc_sf = {
	.name = "qns_mem_noc_sf",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qnm_mnoc_sf },
};

static struct qcom_icc_node qns_nsp0_hsc_noc = {
	.name = "qns_nsp0_hsc_noc",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qnm_nsp0_hscnoc },
};

static struct qcom_icc_node qns_nsp1_hsc_noc = {
	.name = "qns_nsp1_hsc_noc",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qnm_nsp1_hscnoc },
};

static struct qcom_icc_node qns_nsp2_hsc_noc = {
	.name = "qns_nsp2_hsc_noc",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qnm_nsp2_hscnoc },
};

static struct qcom_icc_node qns_nsp3_hsc_noc = {
	.name = "qns_nsp3_hsc_noc",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qnm_nsp3_hscnoc },
};

static struct qcom_icc_node qns_pcie_hscnoc = {
	.name = "qns_pcie_hscnoc",
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.link_nodes = { &qnm_pcie },
};

static struct qcom_icc_node qns_hscnoc_sf = {
	.name = "qns_hscnoc_sf",
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.link_nodes = { &qnm_snoc_sf },
};

static struct qcom_icc_node qhm_qup2 = {
	.name = "qhm_qup2",
	.channels = 1,
	.buswidth = 4,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x1b000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_hscnoc },
};

static struct qcom_icc_node qxm_crypto_0 = {
	.name = "qxm_crypto_0",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x1c000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_hscnoc },
};

static struct qcom_icc_node qxm_crypto_1 = {
	.name = "qxm_crypto_1",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x1d000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_hscnoc },
};

static struct qcom_icc_node qxm_crypto_2 = {
	.name = "qxm_crypto_2",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x1e000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_hscnoc },
};

static struct qcom_icc_node xm_sdc4 = {
	.name = "xm_sdc4",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x16000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_hscnoc },
};

static struct qcom_icc_node xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x17000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_hscnoc },
};

static struct qcom_icc_node xm_usb2 = {
	.name = "xm_usb2",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x19000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_hscnoc },
};

static struct qcom_icc_node xm_usb3_0 = {
	.name = "xm_usb3_0",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x18000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_hscnoc },
};

static struct qcom_icc_node xm_usb3_1 = {
	.name = "xm_usb3_1",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x1a000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_hscnoc },
};

static struct qcom_icc_node qhm_qup0 = {
	.name = "qhm_qup0",
	.channels = 1,
	.buswidth = 4,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x13000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a2noc_hscnoc },
};

static struct qcom_icc_node qhm_qup1 = {
	.name = "qhm_qup1",
	.channels = 1,
	.buswidth = 4,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x14000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a2noc_hscnoc },
};

static struct qcom_icc_node xm_emac_0 = {
	.name = "xm_emac_0",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x16000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a2noc_hscnoc },
};

static struct qcom_icc_node xm_emac_1 = {
	.name = "xm_emac_1",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x17000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a2noc_hscnoc },
};

static struct qcom_icc_node qnm_hpass_dsp0 = {
	.name = "qnm_hpass_dsp0",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_hpass_agnoc_audio },
};

static struct qcom_icc_node qnm_hpass_dsp1 = {
	.name = "qnm_hpass_dsp1",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_hpass_agnoc_audio },
};

static struct qcom_icc_node qnm_hpass_dsp2 = {
	.name = "qnm_hpass_dsp2",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_hpass_agnoc_audio },
};

static struct qcom_icc_node qnm_camnoc_hf = {
	.name = "qnm_camnoc_hf",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x57000, 0x58000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_hf },
};

static struct qcom_icc_node qnm_camnoc_nrt_icp_sf = {
	.name = "qnm_camnoc_nrt_icp_sf",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x1a000 },
		.prio = 4,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_camnoc_rt_cdm_sf = {
	.name = "qnm_camnoc_rt_cdm_sf",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x5b000 },
		.prio = 2,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_camnoc_sf = {
	.name = "qnm_camnoc_sf",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x1b000, 0x1c000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_dprx0 = {
	.name = "qnm_dprx0",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x5c000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_hf },
};

static struct qcom_icc_node qnm_dprx1 = {
	.name = "qnm_dprx1",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x5d000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_hf },
};

static struct qcom_icc_node qnm_mdp0 = {
	.name = "qnm_mdp0",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x59000, 0x5a000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_hf },
};

static struct qcom_icc_node qnm_mdp1 = {
	.name = "qnm_mdp1",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x5e000, 0x5f000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_hf },
};

static struct qcom_icc_node qnm_video_cv_cpu = {
	.name = "qnm_video_cv_cpu",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x21000 },
		.prio = 4,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_video_eva = {
	.name = "qnm_video_eva",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x22000, 0x23000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_video_mvp0 = {
	.name = "qnm_video_mvp0",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x1d000, 0x1e000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_video_mvp1 = {
	.name = "qnm_video_mvp1",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x1f000, 0x20000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_video_v_cpu = {
	.name = "qnm_video_v_cpu",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x24000 },
		.prio = 4,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_nsp_data00 = {
	.name = "qnm_nsp_data00",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_nsp0_hsc_noc },
};

static struct qcom_icc_node qnm_nsp_data01 = {
	.name = "qnm_nsp_data01",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_nsp1_hsc_noc },
};

static struct qcom_icc_node qnm_nsp_data02 = {
	.name = "qnm_nsp_data02",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_nsp2_hsc_noc },
};

static struct qcom_icc_node qnm_nsp_data03 = {
	.name = "qnm_nsp_data03",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_nsp3_hsc_noc },
};

static struct qcom_icc_node qxm_pcie_dma_0 = {
	.name = "qxm_pcie_dma_0",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x49000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 0,
	},
	.num_links = 2,
	.link_nodes = { &qns_pcie_hscnoc, &qns_pcie_obnoc_dma },
};

static struct qcom_icc_node qxm_pcie_dma_1 = {
	.name = "qxm_pcie_dma_1",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x4a000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 0,
	},
	.num_links = 2,
	.link_nodes = { &qns_pcie_hscnoc, &qns_pcie_obnoc_dma },
};

static struct qcom_icc_node qxm_pcie_dma_2 = {
	.name = "qxm_pcie_dma_2",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x4b000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 0,
	},
	.num_links = 2,
	.link_nodes = { &qns_pcie_hscnoc, &qns_pcie_obnoc_dma },
};

static struct qcom_icc_node xm_pcie_0 = {
	.name = "xm_pcie_0",
	.channels = 1,
	.buswidth = 64,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x18000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_pcie_hscnoc },
};

static struct qcom_icc_node xm_pcie_1 = {
	.name = "xm_pcie_1",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x19000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_pcie_hscnoc },
};

static struct qcom_icc_node xm_pcie_2 = {
	.name = "xm_pcie_2",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x1a000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_pcie_hscnoc },
};

static struct qcom_icc_node xm_pcie_3 = {
	.name = "xm_pcie_3",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x1b000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_pcie_hscnoc },
};

static struct qcom_icc_node qnm_aggre1_noc = {
	.name = "qnm_aggre1_noc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qns_hscnoc_sf },
};

static struct qcom_icc_node qnm_aggre2_noc = {
	.name = "qnm_aggre2_noc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qns_hscnoc_sf },
};

static struct qcom_icc_node qnm_cnoc_data = {
	.name = "qnm_cnoc_data",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x1a000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_hscnoc_sf },
};

static struct qcom_icc_node qnm_nsi_noc = {
	.name = "qnm_nsi_noc",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x19000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_hscnoc_sf },
};

static struct qcom_icc_node qnm_safe_dma = {
	.name = "qnm_safe_dma",
	.channels = 1,
	.buswidth = 64,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x1b000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = { &qns_hscnoc_sf },
};

static struct qcom_icc_node qns_a1noc_snoc = {
	.name = "qns_a1noc_snoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_aggre1_noc },
};

static struct qcom_icc_node qns_a2noc_snoc = {
	.name = "qns_a2noc_snoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_aggre2_noc },
};

static struct qcom_icc_node qhm_qspi = {
	.name = "qhm_qspi",
	.channels = 1,
	.buswidth = 4,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x18000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node qnm_sailss_md1 = {
	.name = "qnm_sailss_md1",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x16000 },
		.prio = 2,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node qxm_qup3 = {
	.name = "qxm_qup3",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x17000 },
		.prio = 2,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node qxm_ipa = {
	.name = "qxm_ipa",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x13000 },
		.prio = 2,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_node qxm_soccp = {
	.name = "qxm_soccp",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x16000 },
		.prio = 2,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_node xm_qdss_etr_0 = {
	.name = "xm_qdss_etr_0",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x14000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_node xm_qdss_etr_1 = {
	.name = "xm_qdss_etr_1",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x15000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_bcm bcm_c0n0 = {
	.name = "C0N0",
	.enable_mask = BIT(0),
	.num_nodes = 2,
	.nodes = { &qnm_nsp_data00, &qns_nsp0_hsc_noc },
};

static struct qcom_icc_bcm bcm_c1n0 = {
	.name = "C1N0",
	.enable_mask = BIT(0),
	.num_nodes = 2,
	.nodes = { &qnm_nsp_data01, &qns_nsp1_hsc_noc },
};

static struct qcom_icc_bcm bcm_c2n0 = {
	.name = "C2N0",
	.enable_mask = BIT(0),
	.num_nodes = 2,
	.nodes = { &qnm_nsp_data02, &qns_nsp2_hsc_noc },
};

static struct qcom_icc_bcm bcm_c3n0 = {
	.name = "C3N0",
	.enable_mask = BIT(0),
	.num_nodes = 2,
	.nodes = { &qnm_nsp_data03, &qns_nsp3_hsc_noc },
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.num_nodes = 1,
	.nodes = { &qxm_crypto_0 },
};

static struct qcom_icc_bcm bcm_ce1 = {
	.name = "CE1",
	.num_nodes = 1,
	.nodes = { &qxm_crypto_1 },
};

static struct qcom_icc_bcm bcm_ce2 = {
	.name = "CE2",
	.num_nodes = 1,
	.nodes = { &qxm_crypto_2 },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.keepalive = true,
	.enable_mask = BIT(0),
	.num_nodes = 6,
	.nodes = { &qsm_cfg, &qhm_mm_rscc,
		   &qnm_hscnoc, &qnm_cnoc_pcie_dma,
		   &qnm_hscnoc_pcie, &qnm_pcie_ibnoc_dma },
};

static struct qcom_icc_bcm bcm_cn1 = {
	.name = "CN1",
	.num_nodes = 2,
	.nodes = { &qhs_display_1_cfg, &qhs_display_cfg },
};

static struct qcom_icc_bcm bcm_mc0 = {
	.name = "MC0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_mm0 = {
	.name = "MM0",
	.num_nodes = 1,
	.nodes = { &qns_mem_noc_hf },
};

static struct qcom_icc_bcm bcm_mm1 = {
	.name = "MM1",
	.enable_mask = BIT(0),
	.num_nodes = 14,
	.nodes = { &qnm_camnoc_hf, &qnm_camnoc_nrt_icp_sf,
		   &qnm_camnoc_rt_cdm_sf, &qnm_camnoc_sf,
		   &qnm_dprx0, &qnm_dprx1,
		   &qnm_mdp0, &qnm_mdp1,
		   &qnm_video_cv_cpu, &qnm_video_eva,
		   &qnm_video_mvp0, &qnm_video_mvp1,
		   &qnm_video_v_cpu, &qns_mem_noc_sf },
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.vote_scale = 1,
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qup0_core_slave },
};

static struct qcom_icc_bcm bcm_qup1 = {
	.name = "QUP1",
	.vote_scale = 1,
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qup1_core_slave },
};

static struct qcom_icc_bcm bcm_qup2 = {
	.name = "QUP2",
	.vote_scale = 1,
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qup2_core_slave },
};

static struct qcom_icc_bcm bcm_qup3 = {
	.name = "QUP3",
	.vote_scale = 1,
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qup3_core_slave },
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_sh1 = {
	.name = "SH1",
	.enable_mask = BIT(0),
	.num_nodes = 24,
	.nodes = { &alm_gpu_tcu, &alm_qtc,
		   &alm_sys_tcu0, &alm_sys_tcu1,
		   &alm_sys_tcu2, &chm_apps,
		   &qnm_aggre_north, &qnm_aggre_south,
		   &qnm_gpu0, &qnm_gpu1,
		   &qnm_hpass_adas_hscnoc, &qnm_hpass_audio_hscnoc,
		   &qnm_mnoc_hf, &qnm_mnoc_sf,
		   &qnm_nsp0_hscnoc, &qnm_nsp1_hscnoc,
		   &qnm_nsp2_hscnoc, &qnm_nsp3_hscnoc,
		   &qnm_pcie, &qnm_sailss_md0_hscnoc,
		   &qnm_snoc_sf, &xm_gic,
		   &qns_hscnoc_cnoc, &qns_pcie },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_hscnoc_sf },
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.enable_mask = BIT(0),
	.num_nodes = 14,
	.nodes = { &qns_a1noc_hscnoc, &qns_a2noc_hscnoc,
		   &qxm_pcie_dma_0, &qxm_pcie_dma_1,
		   &qxm_pcie_dma_2, &xm_pcie_0,
		   &xm_pcie_1, &xm_pcie_2,
		   &xm_pcie_3, &qns_pcie_hscnoc,
		   &qns_pcie_obnoc_dma, &qnm_cnoc_data,
		   &qnm_nsi_noc, &qnm_safe_dma },
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.num_nodes = 1,
	.nodes = { &qnm_aggre1_noc },
};

static struct qcom_icc_bcm bcm_sn3 = {
	.name = "SN3",
	.num_nodes = 1,
	.nodes = { &qnm_aggre2_noc },
};

static struct qcom_icc_node * const aggre1_noc_nodes[] = {
	[MASTER_QSPI_0] = &qhm_qspi,
	[MASTER_SAILSS_MD1] = &qnm_sailss_md1,
	[MASTER_QUP_3] = &qxm_qup3,
	[SLAVE_A1NOC_SNOC] = &qns_a1noc_snoc,
};

static const struct regmap_config nord_aggre1_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x1c400,
	.fast_io = true,
};

static const struct qcom_icc_desc nord_aggre1_noc = {
	.config = &nord_aggre1_noc_regmap_config,
	.nodes = aggre1_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre1_noc_nodes),
};

static struct qcom_icc_bcm * const aggre1_noc_tile_bcms[] = {
	&bcm_ce0,
	&bcm_ce1,
	&bcm_ce2,
	&bcm_sn1,
};

static struct qcom_icc_node * const aggre1_noc_tile_nodes[] = {
	[MASTER_QUP_2] = &qhm_qup2,
	[MASTER_CRYPTO_CORE0] = &qxm_crypto_0,
	[MASTER_CRYPTO_CORE1] = &qxm_crypto_1,
	[MASTER_CRYPTO_CORE2] = &qxm_crypto_2,
	[MASTER_SDCC_4] = &xm_sdc4,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_USB2] = &xm_usb2,
	[MASTER_USB3_0] = &xm_usb3_0,
	[MASTER_USB3_1] = &xm_usb3_1,
	[SLAVE_A1NOC_HSCNOC] = &qns_a1noc_hscnoc,
};

static const struct regmap_config nord_aggre1_noc_tile_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x23400,
	.fast_io = true,
};

static const struct qcom_icc_desc nord_aggre1_noc_tile = {
	.config = &nord_aggre1_noc_tile_regmap_config,
	.nodes = aggre1_noc_tile_nodes,
	.num_nodes = ARRAY_SIZE(aggre1_noc_tile_nodes),
	.bcms = aggre1_noc_tile_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_noc_tile_bcms),
};

static struct qcom_icc_node * const aggre2_noc_nodes[] = {
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_SOCCP_AGGR_NOC] = &qxm_soccp,
	[MASTER_QDSS_ETR] = &xm_qdss_etr_0,
	[MASTER_QDSS_ETR_1] = &xm_qdss_etr_1,
	[SLAVE_A2NOC_SNOC] = &qns_a2noc_snoc,
};

static const struct regmap_config nord_aggre2_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x1b400,
	.fast_io = true,
};

static const struct qcom_icc_desc nord_aggre2_noc = {
	.config = &nord_aggre2_noc_regmap_config,
	.nodes = aggre2_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre2_noc_nodes),
};

static struct qcom_icc_bcm * const aggre2_noc_tile_bcms[] = {
	&bcm_sn1,
};

static struct qcom_icc_node * const aggre2_noc_tile_nodes[] = {
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_QUP_1] = &qhm_qup1,
	[MASTER_EMAC_0] = &xm_emac_0,
	[MASTER_EMAC_1] = &xm_emac_1,
	[SLAVE_A2NOC_HSCNOC] = &qns_a2noc_hscnoc,
};

static const struct regmap_config nord_aggre2_noc_tile_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x1b400,
	.fast_io = true,
};

static const struct qcom_icc_desc nord_aggre2_noc_tile = {
	.config = &nord_aggre2_noc_tile_regmap_config,
	.nodes = aggre2_noc_tile_nodes,
	.num_nodes = ARRAY_SIZE(aggre2_noc_tile_nodes),
	.bcms = aggre2_noc_tile_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_noc_tile_bcms),
};

static struct qcom_icc_bcm * const clk_virt_bcms[] = {
	&bcm_qup0,
	&bcm_qup1,
	&bcm_qup2,
	&bcm_qup3,
};

static struct qcom_icc_node * const clk_virt_nodes[] = {
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[MASTER_QUP_CORE_1] = &qup1_core_master,
	[MASTER_QUP_CORE_2] = &qup2_core_master,
	[MASTER_QUP_CORE_3] = &qup3_core_master,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[SLAVE_QUP_CORE_1] = &qup1_core_slave,
	[SLAVE_QUP_CORE_2] = &qup2_core_slave,
	[SLAVE_QUP_CORE_3] = &qup3_core_slave,
};

static const struct qcom_icc_desc nord_clk_virt = {
	.nodes = clk_virt_nodes,
	.num_nodes = ARRAY_SIZE(clk_virt_nodes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
};

static struct qcom_icc_bcm * const cnoc_cfg_bcms[] = {
	&bcm_cn0,
	&bcm_cn1,
};

static struct qcom_icc_node * const cnoc_cfg_nodes[] = {
	[MASTER_CNOC_CFG] = &qsm_cfg,
	[SLAVE_PS_ETH_0] = &ps_eth_0,
	[SLAVE_PS_ETH_1] = &ps_eth_1,
	[SLAVE_SHS_SERVER] = &ps_shs_server,
	[SLAVE_AHB2PHY_0] = &qhs_ahb2phy0,
	[SLAVE_AHB2PHY_1] = &qhs_ahb2phy1,
	[SLAVE_AHB2PHY_2] = &qhs_ahb2phy2,
	[SLAVE_AHB2PHY_3] = &qhs_ahb2phy3,
	[SLAVE_AHB2PHY_ETH_0] = &qhs_ahb2phy_eth_0,
	[SLAVE_AHB2PHY_ETH_1] = &qhs_ahb2phy_eth_1,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_CRYPTO_1_CFG] = &qhs_crypto1_cfg,
	[SLAVE_CRYPTO_2_CFG] = &qhs_crypto2_cfg,
	[SLAVE_DISPLAY_1_CFG] = &qhs_display_1_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_DPRX0] = &qhs_dprx0,
	[SLAVE_DPRX1] = &qhs_dprx1,
	[SLAVE_EVA_CFG] = &qhs_eva_cfg,
	[SLAVE_GFX3D_CFG] = &qhs_gpuss_0_cfg,
	[SLAVE_GFX3D_1_CFG] = &qhs_gpuss_1_cfg,
	[SLAVE_I2C] = &qhs_i2c,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_MCW_PCIE] = &qhs_mcw_pcie,
	[SLAVE_MM_RSCC] = &qhs_mm_rscc,
	[SLAVE_NE_CLK_CTL] = &qhs_ne_clk_ctl,
	[SLAVE_NSPSS0_CFG] = &qhs_nspss0_cfg,
	[SLAVE_NSPSS1_CFG] = &qhs_nspss1_cfg,
	[SLAVE_NSPSS2_CFG] = &qhs_nspss2_cfg,
	[SLAVE_NSPSS3_CFG] = &qhs_nspss3_cfg,
	[SLAVE_NW_CLK_CTL] = &qhs_nw_clk_ctl,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QSPI_0] = &qhs_qspi,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_QUP_3] = &qhs_qup3,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_QUP_2] = &qhs_qup2,
	[SLAVE_SAFEDMA_CFG] = &qhs_safedma_cfg,
	[SLAVE_SDCC_4] = &qhs_sdc4,
	[SLAVE_SE_CLK_CTL] = &qhs_se_clk_ctl,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_TSC_CFG] = &qhs_tsc_cfg,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB2] = &qhs_usb2,
	[SLAVE_USB3_0] = &qhs_usb3_0,
	[SLAVE_USB3_1] = &qhs_usb3_1,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_COMPUTENOC_CFG] = &qss_computenoc_cfg,
	[SLAVE_PCIE_NOC_CFG] = &qss_pcie_noc_cfg,
	[SLAVE_QTC_CFG] = &qss_qtc_cfg,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_SYS_TCU0_CFG] = &xs_sys_tcu0_cfg,
	[SLAVE_SYS_TCU1_CFG] = &xs_sys_tcu1_cfg,
	[SLAVE_SYS_TCU2_CFG] = &xs_sys_tcu2_cfg,
};

static const struct regmap_config nord_cnoc_cfg_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xd200,
	.fast_io = true,
};

static const struct qcom_icc_desc nord_cnoc_cfg = {
	.config = &nord_cnoc_cfg_regmap_config,
	.nodes = cnoc_cfg_nodes,
	.num_nodes = ARRAY_SIZE(cnoc_cfg_nodes),
	.bcms = cnoc_cfg_bcms,
	.num_bcms = ARRAY_SIZE(cnoc_cfg_bcms),
};

static struct qcom_icc_bcm * const cnoc_main_bcms[] = {
	&bcm_cn0,
};

static struct qcom_icc_node * const cnoc_main_nodes[] = {
	[MASTER_MM_RSCC] = &qhm_mm_rscc,
	[MASTER_HSCNOC_CNOC] = &qnm_hscnoc,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_HBCU] = &qhs_hbcu,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_IPC_ROUTER_CFG] = &qhs_ipc_router,
	[SLAVE_SOCCP] = &qhs_soccp,
	[SLAVE_TME_CFG] = &qhs_tme_cfg,
	[SLAVE_PCIE_DMA] = &qns_pcie_dma,
	[SLAVE_CNOC_CFG] = &qss_cfg,
	[SLAVE_DDRSS_CFG] = &qss_ddrss_cfg,
	[SLAVE_IMEM] = &qxs_imem,
};

static const struct regmap_config nord_cnoc_main_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x1d200,
	.fast_io = true,
};

static const struct qcom_icc_desc nord_cnoc_main = {
	.config = &nord_cnoc_main_regmap_config,
	.nodes = cnoc_main_nodes,
	.num_nodes = ARRAY_SIZE(cnoc_main_nodes),
	.bcms = cnoc_main_bcms,
	.num_bcms = ARRAY_SIZE(cnoc_main_bcms),
};

static struct qcom_icc_node * const hpass_ag_noc_nodes[] = {
	[MASTER_HPASS_PROC_0] = &qnm_hpass_dsp0,
	[MASTER_HPASS_PROC_1] = &qnm_hpass_dsp1,
	[MASTER_HPASS_PROC_2] = &qnm_hpass_dsp2,
	[SLAVE_HPASS_AGNOC_AUDIO] = &qns_hpass_agnoc_audio,
};

static const struct regmap_config nord_hpass_ag_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x37080,
	.fast_io = true,
};

static const struct qcom_icc_desc nord_hpass_ag_noc = {
	.config = &nord_hpass_ag_noc_regmap_config,
	.nodes = hpass_ag_noc_nodes,
	.num_nodes = ARRAY_SIZE(hpass_ag_noc_nodes),
};

static struct qcom_icc_bcm * const hscnoc_bcms[] = {
	&bcm_sh0,
	&bcm_sh1,
};

static struct qcom_icc_node * const hscnoc_nodes[] = {
	[MASTER_GPU_TCU] = &alm_gpu_tcu,
	[MASTER_QTC_TCU] = &alm_qtc,
	[MASTER_SYS_TCU_0] = &alm_sys_tcu0,
	[MASTER_SYS_TCU_1] = &alm_sys_tcu1,
	[MASTER_SYS_TCU_2] = &alm_sys_tcu2,
	[MASTER_APPSS_PROC] = &chm_apps,
	[MASTER_A1NOC_TILE_HSCNOC] = &qnm_aggre_north,
	[MASTER_A2NOC_TILE_HSCNOC] = &qnm_aggre_south,
	[MASTER_GFX3D] = &qnm_gpu0,
	[MASTER_GFX3D_1] = &qnm_gpu1,
	[MASTER_HPASS_ADAS_HSCNOC] = &qnm_hpass_adas_hscnoc,
	[MASTER_HPASS_AUDIO_HSCNOC] = &qnm_hpass_audio_hscnoc,
	[MASTER_MNOC_HF_MEM_NOC] = &qnm_mnoc_hf,
	[MASTER_MNOC_SF_MEM_NOC] = &qnm_mnoc_sf,
	[MASTER_NSP0_HSCNOC] = &qnm_nsp0_hscnoc,
	[MASTER_NSP1_HSCNOC] = &qnm_nsp1_hscnoc,
	[MASTER_NSP2_HSCNOC] = &qnm_nsp2_hscnoc,
	[MASTER_NSP3_HSCNOC] = &qnm_nsp3_hscnoc,
	[MASTER_ANOC_PCIE_GEM_NOC] = &qnm_pcie,
	[MASTER_SAILSS_MD0_HSCNOC] = &qnm_sailss_md0_hscnoc,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[MASTER_GIC] = &xm_gic,
	[SLAVE_HSCNOC_CNOC] = &qns_hscnoc_cnoc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_MEM_NOC_PCIE_SNOC] = &qns_pcie,
};

static const struct regmap_config nord_hscnoc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xa45080,
	.fast_io = true,
};

static const struct qcom_icc_desc nord_hscnoc = {
	.config = &nord_hscnoc_regmap_config,
	.nodes = hscnoc_nodes,
	.num_nodes = ARRAY_SIZE(hscnoc_nodes),
	.bcms = hscnoc_bcms,
	.num_bcms = ARRAY_SIZE(hscnoc_bcms),
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_mc0,
};

static struct qcom_icc_node * const mc_virt_nodes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
};

static const struct qcom_icc_desc nord_mc_virt = {
	.nodes = mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm * const mmss_noc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
};

static struct qcom_icc_node * const mmss_noc_nodes[] = {
	[MASTER_CAMNOC_HF] = &qnm_camnoc_hf,
	[MASTER_CAMNOC_NRT_ICP_SF] = &qnm_camnoc_nrt_icp_sf,
	[MASTER_CAMNOC_RT_CDM_SF] = &qnm_camnoc_rt_cdm_sf,
	[MASTER_CAMNOC_SF] = &qnm_camnoc_sf,
	[MASTER_DPRX0] = &qnm_dprx0,
	[MASTER_DPRX1] = &qnm_dprx1,
	[MASTER_MDP0] = &qnm_mdp0,
	[MASTER_MDP1] = &qnm_mdp1,
	[MASTER_VIDEO_CV_PROC] = &qnm_video_cv_cpu,
	[MASTER_VIDEO_EVA] = &qnm_video_eva,
	[MASTER_VIDEO_MVP0] = &qnm_video_mvp0,
	[MASTER_VIDEO_MVP1] = &qnm_video_mvp1,
	[MASTER_VIDEO_V_PROC] = &qnm_video_v_cpu,
	[SLAVE_MNOC_HF_MEM_NOC] = &qns_mem_noc_hf,
	[SLAVE_MNOC_SF_MEM_NOC] = &qns_mem_noc_sf,
};

static const struct regmap_config nord_mmss_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x72800,
	.fast_io = true,
};

static const struct qcom_icc_desc nord_mmss_noc = {
	.config = &nord_mmss_noc_regmap_config,
	.nodes = mmss_noc_nodes,
	.num_nodes = ARRAY_SIZE(mmss_noc_nodes),
	.bcms = mmss_noc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_noc_bcms),
};

static struct qcom_icc_bcm * const nsp_data_noc_0_bcms[] = {
	&bcm_c0n0,
};

static struct qcom_icc_node * const nsp_data_noc_0_nodes[] = {
	[MASTER_NSP0_PROC] = &qnm_nsp_data00,
	[SLAVE_NSP0_HSC_NOC] = &qns_nsp0_hsc_noc,
};

static const struct regmap_config nord_nsp_data_noc_0_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x2a200,
	.fast_io = true,
};

static const struct qcom_icc_desc nord_nsp_data_noc_0 = {
	.config = &nord_nsp_data_noc_0_regmap_config,
	.nodes = nsp_data_noc_0_nodes,
	.num_nodes = ARRAY_SIZE(nsp_data_noc_0_nodes),
	.bcms = nsp_data_noc_0_bcms,
	.num_bcms = ARRAY_SIZE(nsp_data_noc_0_bcms),
};

static struct qcom_icc_bcm * const nsp_data_noc_1_bcms[] = {
	&bcm_c1n0,
};

static struct qcom_icc_node * const nsp_data_noc_1_nodes[] = {
	[MASTER_NSP1_PROC] = &qnm_nsp_data01,
	[SLAVE_NSP1_HSC_NOC] = &qns_nsp1_hsc_noc,
};

static const struct regmap_config nord_nsp_data_noc_1_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x2a200,
	.fast_io = true,
};

static const struct qcom_icc_desc nord_nsp_data_noc_1 = {
	.config = &nord_nsp_data_noc_1_regmap_config,
	.nodes = nsp_data_noc_1_nodes,
	.num_nodes = ARRAY_SIZE(nsp_data_noc_1_nodes),
	.bcms = nsp_data_noc_1_bcms,
	.num_bcms = ARRAY_SIZE(nsp_data_noc_1_bcms),
};

static struct qcom_icc_bcm * const nsp_data_noc_2_bcms[] = {
	&bcm_c2n0,
};

static struct qcom_icc_node * const nsp_data_noc_2_nodes[] = {
	[MASTER_NSP2_PROC] = &qnm_nsp_data02,
	[SLAVE_NSP2_HSC_NOC] = &qns_nsp2_hsc_noc,
};

static const struct regmap_config nord_nsp_data_noc_2_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x2a200,
	.fast_io = true,
};

static const struct qcom_icc_desc nord_nsp_data_noc_2 = {
	.config = &nord_nsp_data_noc_2_regmap_config,
	.nodes = nsp_data_noc_2_nodes,
	.num_nodes = ARRAY_SIZE(nsp_data_noc_2_nodes),
	.bcms = nsp_data_noc_2_bcms,
	.num_bcms = ARRAY_SIZE(nsp_data_noc_2_bcms),
};

static struct qcom_icc_bcm * const nsp_data_noc_3_bcms[] = {
	&bcm_c3n0,
};

static struct qcom_icc_node * const nsp_data_noc_3_nodes[] = {
	[MASTER_NSP3_PROC] = &qnm_nsp_data03,
	[SLAVE_NSP3_HSC_NOC] = &qns_nsp3_hsc_noc,
};

static const struct regmap_config nord_nsp_data_noc_3_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x2a200,
	.fast_io = true,
};

static const struct qcom_icc_desc nord_nsp_data_noc_3 = {
	.config = &nord_nsp_data_noc_3_regmap_config,
	.nodes = nsp_data_noc_3_nodes,
	.num_nodes = ARRAY_SIZE(nsp_data_noc_3_nodes),
	.bcms = nsp_data_noc_3_bcms,
	.num_bcms = ARRAY_SIZE(nsp_data_noc_3_bcms),
};

static struct qcom_icc_node * const pcie_cfg_nodes[] = {
	[MASTER_PCIE_NOC_CFG] = &qsm_pcie_noc_cfg,
	[SLAVE_PCIE_AHB2PHY_CFG] = &qhs_pcie_ahb2phy_cfg,
	[SLAVE_PCIE_CFG_0] = &qhs_pcie_cfg_0,
	[SLAVE_PCIE_CFG_1] = &qhs_pcie_cfg_1,
	[SLAVE_PCIE_CFG_2] = &qhs_pcie_cfg_2,
	[SLAVE_PCIE_CFG_3] = &qhs_pcie_cfg_3,
	[SLAVE_PCIE_DMA_0_CFG] = &qhs_pcie_dma_0_cfg,
	[SLAVE_PCIE_DMA_1_CFG] = &qhs_pcie_dma_1_cfg,
	[SLAVE_PCIE_DMA_2_CFG] = &qhs_pcie_dma_2_cfg,
};

static const struct regmap_config nord_pcie_cfg_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x7200,
	.fast_io = true,
};

static const struct qcom_icc_desc nord_pcie_cfg = {
	.config = &nord_pcie_cfg_regmap_config,
	.nodes = pcie_cfg_nodes,
	.num_nodes = ARRAY_SIZE(pcie_cfg_nodes),
};

static struct qcom_icc_bcm * const pcie_data_inbound_bcms[] = {
	&bcm_sn1,
};

static struct qcom_icc_node * const pcie_data_inbound_nodes[] = {
	[MASTER_PCIE_DMA_0] = &qxm_pcie_dma_0,
	[MASTER_PCIE_DMA_1] = &qxm_pcie_dma_1,
	[MASTER_PCIE_DMA_2] = &qxm_pcie_dma_2,
	[MASTER_PCIE_0] = &xm_pcie_0,
	[MASTER_PCIE_1] = &xm_pcie_1,
	[MASTER_PCIE_2] = &xm_pcie_2,
	[MASTER_PCIE_3] = &xm_pcie_3,
	[SLAVE_PCIE_HSCNOC] = &qns_pcie_hscnoc,
	[SLAVE_PCIE_OBNOC_DMA] = &qns_pcie_obnoc_dma,
};

static const struct regmap_config nord_pcie_data_inbound_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x4b080,
	.fast_io = true,
};

static const struct qcom_icc_desc nord_pcie_data_inbound = {
	.config = &nord_pcie_data_inbound_regmap_config,
	.nodes = pcie_data_inbound_nodes,
	.num_nodes = ARRAY_SIZE(pcie_data_inbound_nodes),
	.bcms = pcie_data_inbound_bcms,
	.num_bcms = ARRAY_SIZE(pcie_data_inbound_bcms),
};

static struct qcom_icc_bcm * const pcie_data_outbound_bcms[] = {
	&bcm_cn0,
};

static struct qcom_icc_node * const pcie_data_outbound_nodes[] = {
	[MASTER_CNOC_PCIE_DMA] = &qnm_cnoc_pcie_dma,
	[MASTER_ANOC_PCIE_HSCNOC] = &qnm_hscnoc_pcie,
	[MASTER_PCIE_IBNOC_DMA] = &qnm_pcie_ibnoc_dma,
	[SLAVE_PCIE_DMA_0] = &qxs_pcie_dma_0,
	[SLAVE_PCIE_DMA_1] = &qxs_pcie_dma_1,
	[SLAVE_PCIE_DMA_2] = &qxs_pcie_dma_2,
	[SLAVE_PCIE_0] = &xs_pcie_0,
	[SLAVE_PCIE_1] = &xs_pcie_1,
	[SLAVE_PCIE_2] = &xs_pcie_2,
	[SLAVE_PCIE_3] = &xs_pcie_3,
};

static const struct regmap_config nord_pcie_data_outbound_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x17000,
	.fast_io = true,
};

static const struct qcom_icc_desc nord_pcie_data_outbound = {
	.config = &nord_pcie_data_outbound_regmap_config,
	.nodes = pcie_data_outbound_nodes,
	.num_nodes = ARRAY_SIZE(pcie_data_outbound_nodes),
	.bcms = pcie_data_outbound_bcms,
	.num_bcms = ARRAY_SIZE(pcie_data_outbound_bcms),
};

static struct qcom_icc_bcm * const system_noc_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn2,
	&bcm_sn3,
};

static struct qcom_icc_node * const system_noc_nodes[] = {
	[MASTER_A1NOC_SNOC] = &qnm_aggre1_noc,
	[MASTER_A2NOC_SNOC] = &qnm_aggre2_noc,
	[MASTER_CNOC_SNOC] = &qnm_cnoc_data,
	[MASTER_NSINOC_SNOC] = &qnm_nsi_noc,
	[MASTER_SAFE_DMA] = &qnm_safe_dma,
	[SLAVE_SNOC_HSCNOC_SF] = &qns_hscnoc_sf,
};

static const struct regmap_config nord_system_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x1c080,
	.fast_io = true,
};

static const struct qcom_icc_desc nord_system_noc = {
	.config = &nord_system_noc_regmap_config,
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,nord-aggre1-noc", .data = &nord_aggre1_noc },
	{ .compatible = "qcom,nord-aggre1-noc-tile", .data = &nord_aggre1_noc_tile },
	{ .compatible = "qcom,nord-aggre2-noc", .data = &nord_aggre2_noc },
	{ .compatible = "qcom,nord-aggre2-noc-tile", .data = &nord_aggre2_noc_tile },
	{ .compatible = "qcom,nord-clk-virt", .data = &nord_clk_virt },
	{ .compatible = "qcom,nord-cnoc-cfg", .data = &nord_cnoc_cfg },
	{ .compatible = "qcom,nord-cnoc-main", .data = &nord_cnoc_main },
	{ .compatible = "qcom,nord-hpass-ag-noc", .data = &nord_hpass_ag_noc },
	{ .compatible = "qcom,nord-hscnoc", .data = &nord_hscnoc },
	{ .compatible = "qcom,nord-mc-virt", .data = &nord_mc_virt },
	{ .compatible = "qcom,nord-mmss-noc", .data = &nord_mmss_noc },
	{ .compatible = "qcom,nord-nsp-data-noc-0", .data = &nord_nsp_data_noc_0 },
	{ .compatible = "qcom,nord-nsp-data-noc-1", .data = &nord_nsp_data_noc_1 },
	{ .compatible = "qcom,nord-nsp-data-noc-2", .data = &nord_nsp_data_noc_2 },
	{ .compatible = "qcom,nord-nsp-data-noc-3", .data = &nord_nsp_data_noc_3 },
	{ .compatible = "qcom,nord-pcie-cfg", .data = &nord_pcie_cfg },
	{ .compatible = "qcom,nord-pcie-data-inbound", .data = &nord_pcie_data_inbound },
	{ .compatible = "qcom,nord-pcie-data-outbound", .data = &nord_pcie_data_outbound },
	{ .compatible = "qcom,nord-system-noc", .data = &nord_system_noc },
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-nord",
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

MODULE_DESCRIPTION("Qualcomm Nord NoC driver");
MODULE_LICENSE("GPL");
