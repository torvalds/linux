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
#include <dt-bindings/interconnect/qcom,hawi-rpmh.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"

static struct qcom_icc_node ddr_eff_veto_slave = {
	.name = "ddr_eff_veto_slave",
	.channels = 1,
	.buswidth = 4,
};

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

static struct qcom_icc_node qup4_core_slave = {
	.name = "qup4_core_slave",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_aoss = {
	.name = "qhs_aoss",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ipa = {
	.name = "qhs_ipa",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ipc_router_fence = {
	.name = "qhs_ipc_router_fence",
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

static struct qcom_icc_node xs_pcie = {
	.name = "xs_pcie",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node xs_pcie_g4x1 = {
	.name = "xs_pcie_g4x1",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qhs_lpi_cc = {
	.name = "qhs_lpi_cc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_lb = {
	.name = "qns_lb",
	.channels = 4,
	.buswidth = 32,
};

static struct qcom_icc_node srvc_llclpi_noc = {
	.name = "srvc_llclpi_noc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_llclpi_noc_chipcx = {
	.name = "srvc_llclpi_noc_chipcx",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.channels = 4,
	.buswidth = 4,
};

static struct qcom_icc_node ddr_rt_slave = {
	.name = "ddr_rt_slave",
	.channels = 4,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_pcie_aggre_noc = {
	.name = "srvc_pcie_aggre_noc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ahb2phy0 = {
	.name = "qhs_ahb2phy0",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_boot_rom = {
	.name = "qhs_boot_rom",
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

static struct qcom_icc_node qhs_crypto_cfg = {
	.name = "qhs_crypto_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_display_cfg = {
	.name = "qhs_display_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_eva_cfg = {
	.name = "qhs_eva_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
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

static struct qcom_icc_node qhs_ipc_router = {
	.name = "qhs_ipc_router",
	.channels = 4,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_iris_cfg = {
	.name = "qhs_iris_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_mss_cfg = {
	.name = "qhs_mss_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie_cfg = {
	.name = "qhs_pcie_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie_g4x1_cfg = {
	.name = "qhs_pcie_g4x1_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_prng = {
	.name = "qhs_prng",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qspi = {
	.name = "qhs_qspi",
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

static struct qcom_icc_node qhs_qup3 = {
	.name = "qhs_qup3",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qup4 = {
	.name = "qhs_qup4",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_sdc2 = {
	.name = "qhs_sdc2",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_sdc4 = {
	.name = "qhs_sdc4",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_tlmm = {
	.name = "qhs_tlmm",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb3 = {
	.name = "qhs_usb3",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qss_qdss_cfg = {
	.name = "qss_qdss_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qss_qdss_stm = {
	.name = "qss_qdss_stm",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qss_tcsr = {
	.name = "qss_tcsr",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node ddr_eff_veto_master = {
	.name = "ddr_eff_veto_master",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &ddr_eff_veto_slave },
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

static struct qcom_icc_node qup4_core_master = {
	.name = "qup4_core_master",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qup4_core_slave },
};

static struct qcom_icc_node qnm_gemnoc_pcie = {
	.name = "qnm_gemnoc_pcie",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = { &xs_pcie, &xs_pcie_g4x1 },
};

static struct qcom_icc_node qnm_lpiaon_noc_llclpi_noc = {
	.name = "qnm_lpiaon_noc_llclpi_noc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 4,
	.link_nodes = { &qhs_lpi_cc, &qns_lb,
			&srvc_llclpi_noc, &srvc_llclpi_noc_chipcx },
};

static struct qcom_icc_node llcc_mc = {
	.name = "llcc_mc",
	.channels = 4,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &ebi },
};

static struct qcom_icc_node ddr_rt_mc = {
	.name = "ddr_rt_mc",
	.channels = 4,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &ddr_rt_slave },
};

static struct qcom_icc_node qsm_pcie_anoc_cfg = {
	.name = "qsm_pcie_anoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &srvc_pcie_aggre_noc },
};

static struct qcom_icc_node qsm_cfg_east = {
	.name = "qsm_cfg_east",
	.channels = 1,
	.buswidth = 4,
	.num_links = 4,
	.link_nodes = { &qhs_crypto_cfg, &qhs_gpuss_cfg,
			&qhs_qup2, &qhs_vsense_ctrl_cfg },
};

static struct qcom_icc_node qsm_cfg_mm = {
	.name = "qsm_cfg_mm",
	.channels = 1,
	.buswidth = 4,
	.num_links = 5,
	.link_nodes = { &qhs_boot_rom, &qhs_camera_cfg,
			&qhs_display_cfg, &qhs_eva_cfg,
			&qhs_iris_cfg },
};

static struct qcom_icc_node qsm_cfg_north = {
	.name = "qsm_cfg_north",
	.channels = 1,
	.buswidth = 4,
	.num_links = 5,
	.link_nodes = { &qhs_pcie_cfg, &qhs_pcie_g4x1_cfg,
			&qhs_qup3, &qhs_qup4,
			&qhs_sdc2 },
};

static struct qcom_icc_node qsm_cfg_south = {
	.name = "qsm_cfg_south",
	.channels = 1,
	.buswidth = 4,
	.num_links = 6,
	.link_nodes = { &qhs_ahb2phy0, &qhs_qspi,
			&qhs_qup1, &qhs_sdc4,
			&qhs_ufs_mem_cfg, &qhs_usb3 },
};

static struct qcom_icc_node qsm_cfg_southwest = {
	.name = "qsm_cfg_southwest",
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.link_nodes = { &qhs_ipc_router, &qhs_mss_cfg },
};

static struct qcom_icc_node qns_llcc = {
	.name = "qns_llcc",
	.channels = 4,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &llcc_mc },
};

static struct qcom_icc_node qns_pcie = {
	.name = "qns_pcie",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qnm_gemnoc_pcie },
};

static struct qcom_icc_node qns_llc_lpinoc = {
	.name = "qns_llc_lpinoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_lpiaon_noc_llclpi_noc },
};

static struct qcom_icc_node qss_pcie_anoc_cfg = {
	.name = "qss_pcie_anoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qsm_pcie_anoc_cfg },
};

static struct qcom_icc_node qss_stdst_east_cfg = {
	.name = "qss_stdst_east_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qsm_cfg_east },
};

static struct qcom_icc_node qss_stdst_mm_cfg = {
	.name = "qss_stdst_mm_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qsm_cfg_mm },
};

static struct qcom_icc_node qss_stdst_north_cfg = {
	.name = "qss_stdst_north_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qsm_cfg_north },
};

static struct qcom_icc_node qss_stdst_south_cfg = {
	.name = "qss_stdst_south_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qsm_cfg_south },
};

static struct qcom_icc_node qss_stdst_southwest_cfg = {
	.name = "qss_stdst_southwest_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qsm_cfg_southwest },
};

static struct qcom_icc_node alm_gic = {
	.name = "alm_gic",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x14d000 },
		.prio = 4,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_llcc },
};

static struct qcom_icc_node qnm_qpace = {
	.name = "qnm_qpace",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x153000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_llcc },
};

static struct qcom_icc_node qsm_cfg_center = {
	.name = "qsm_cfg_center",
	.channels = 1,
	.buswidth = 4,
	.num_links = 10,
	.link_nodes = { &qhs_clk_ctl, &qhs_i2c,
			&qhs_imem_cfg, &qhs_prng,
			&qhs_tlmm, &qss_pcie_anoc_cfg,
			&qss_qdss_cfg, &qss_qdss_stm,
			&qss_tcsr, &xs_sys_tcu_cfg },
};

static struct qcom_icc_node qss_stdst_center_cfg = {
	.name = "qss_stdst_center_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qsm_cfg_center },
};

static struct qcom_icc_node qsm_cnoc_main = {
	.name = "qsm_cnoc_main",
	.channels = 1,
	.buswidth = 4,
	.num_links = 6,
	.link_nodes = { &qss_stdst_center_cfg, &qss_stdst_east_cfg,
			&qss_stdst_mm_cfg, &qss_stdst_north_cfg,
			&qss_stdst_south_cfg, &qss_stdst_southwest_cfg },
};

static struct qcom_icc_node qss_cfg = {
	.name = "qss_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qsm_cnoc_main },
};

static struct qcom_icc_node qnm_gemnoc_cnoc = {
	.name = "qnm_gemnoc_cnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 8,
	.link_nodes = { &qhs_aoss, &qhs_ipa,
			&qhs_ipc_router_fence, &qhs_soccp,
			&qhs_tme_cfg, &qss_cfg,
			&qss_ddrss_cfg, &qxs_imem },
};

static struct qcom_icc_node qns_gem_noc_cnoc = {
	.name = "qns_gem_noc_cnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_gemnoc_cnoc },
};

static struct qcom_icc_node alm_gpu_tcu = {
	.name = "alm_gpu_tcu",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x145000 },
		.prio = 1,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 2,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc },
};

static struct qcom_icc_node alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x147000 },
		.prio = 6,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 2,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc },
};

static struct qcom_icc_node chm_apps = {
	.name = "chm_apps",
	.channels = 4,
	.buswidth = 32,
	.num_links = 3,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_gpu = {
	.name = "qnm_gpu",
	.channels = 4,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 4,
		.port_offsets = { 0x51000, 0x53000, 0xd1000, 0xd3000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 3,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_lpass_gemnoc = {
	.name = "qnm_lpass_gemnoc",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x149000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 3,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_mdsp = {
	.name = "qnm_mdsp",
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_mnoc_hf = {
	.name = "qnm_mnoc_hf",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x55000, 0xd5000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 3,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_mnoc_sf = {
	.name = "qnm_mnoc_sf",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x57000, 0xd7000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 3,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_nsp_gemnoc = {
	.name = "qnm_nsp_gemnoc",
	.channels = 4,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 4,
		.port_offsets = { 0x59000, 0x5b000, 0xd9000, 0xdb000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 3,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_pcie = {
	.name = "qnm_pcie",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x14b000 },
		.prio = 2,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 2,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc },
};

static struct qcom_icc_node qnm_snoc_sf = {
	.name = "qnm_snoc_sf",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x14f000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 3,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_wlan_q6 = {
	.name = "qnm_wlan_q6",
	.channels = 1,
	.buswidth = 8,
	.num_links = 3,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qns_lpass_ag_noc_gemnoc = {
	.name = "qns_lpass_ag_noc_gemnoc",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qnm_lpass_gemnoc },
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

static struct qcom_icc_node qns_nsp_gemnoc = {
	.name = "qns_nsp_gemnoc",
	.channels = 4,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qnm_nsp_gemnoc },
};

static struct qcom_icc_node qns_pcie_gemnoc = {
	.name = "qns_pcie_gemnoc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qnm_pcie },
};

static struct qcom_icc_node qns_gemnoc_sf = {
	.name = "qns_gemnoc_sf",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qnm_snoc_sf },
};

static struct qcom_icc_node qnm_lpiaon_noc = {
	.name = "qnm_lpiaon_noc",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_lpass_ag_noc_gemnoc },
};

static struct qcom_icc_node qnm_camnoc_hf = {
	.name = "qnm_camnoc_hf",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x2a000, 0x2b000 },
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
		.port_offsets = { 0x2c000 },
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
		.port_offsets = { 0x38000 },
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
		.port_offsets = { 0x2d000, 0x2e000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_mdp = {
	.name = "qnm_mdp",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x2f000, 0x30000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_hf },
};

static struct qcom_icc_node qnm_mdss_dcp = {
	.name = "qnm_mdss_dcp",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x39000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_vapss_hcp = {
	.name = "qnm_vapss_hcp",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_video_cv_cpu = {
	.name = "qnm_video_cv_cpu",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x34000 },
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
		.port_offsets = { 0x35000, 0x36000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_video_mvp = {
	.name = "qnm_video_mvp",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x32000, 0x33000 },
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
		.port_offsets = { 0x37000 },
		.prio = 4,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_nsp = {
	.name = "qnm_nsp",
	.channels = 4,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_nsp_gemnoc },
};

static struct qcom_icc_node xm_pcie = {
	.name = "xm_pcie",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xc000 },
		.prio = 3,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_pcie_gemnoc },
};

static struct qcom_icc_node xm_pcie_g4x1 = {
	.name = "xm_pcie_g4x1",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xd000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_pcie_gemnoc },
};

static struct qcom_icc_node qnm_aggre_noc = {
	.name = "qnm_aggre_noc",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x20000 },
		.prio = 2,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_node qnm_apss_noc = {
	.name = "qnm_apss_noc",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x1e000 },
		.prio = 2,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_node qnm_cnoc_data = {
	.name = "qnm_cnoc_data",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x1f000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_node qns_a1noc_snoc = {
	.name = "qns_a1noc_snoc",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qnm_aggre_noc },
};

static struct qcom_icc_node qns_lpass_aggnoc = {
	.name = "qns_lpass_aggnoc",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qnm_lpiaon_noc },
};

static struct qcom_icc_node qhm_qspi = {
	.name = "qhm_qspi",
	.channels = 1,
	.buswidth = 4,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x49000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node qhm_qup2 = {
	.name = "qhm_qup2",
	.channels = 1,
	.buswidth = 4,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x48000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node qhm_qup3 = {
	.name = "qhm_qup3",
	.channels = 1,
	.buswidth = 4,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x46000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node qhm_qup4 = {
	.name = "qhm_qup4",
	.channels = 1,
	.buswidth = 4,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x47000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node qxm_crypto = {
	.name = "qxm_crypto",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x40000 },
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
	.buswidth = 16,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x41000 },
		.prio = 2,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node qxm_qup1 = {
	.name = "qxm_qup1",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x4d000 },
		.prio = 2,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node qxm_soccp = {
	.name = "qxm_soccp",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x45000 },
		.prio = 2,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node xm_qdss_etr_0 = {
	.name = "xm_qdss_etr_0",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x42000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node xm_qdss_etr_1 = {
	.name = "xm_qdss_etr_1",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x43000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node xm_sdc2 = {
	.name = "xm_sdc2",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x44000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node xm_sdc4 = {
	.name = "xm_sdc4",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x4a000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x4b000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node xm_usb3 = {
	.name = "xm_usb3",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x4c000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node qnm_lpass_lpinoc = {
	.name = "qnm_lpass_lpinoc",
	.channels = 1,
	.buswidth = 32,
	.num_links = 2,
	.link_nodes = { &qns_llc_lpinoc, &qns_lpass_aggnoc },
};

static struct qcom_icc_node qns_lpi_aon_noc = {
	.name = "qns_lpi_aon_noc",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qnm_lpass_lpinoc },
};

static struct qcom_icc_node qnm_lpinoc_dsp_qns4m = {
	.name = "qnm_lpinoc_dsp_qns4m",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_lpi_aon_noc },
};

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
	.enable_mask = BIT(3),
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
	.enable_mask = BIT(0),
	.keepalive = true,
	.num_nodes = 24,
	.nodes = { &qnm_gemnoc_cnoc, &qnm_gemnoc_pcie,
		   &qhs_aoss, &qhs_ipa,
		   &qhs_ipc_router_fence, &qhs_soccp,
		   &qhs_tme_cfg, &qss_cfg,
		   &qss_ddrss_cfg, &qxs_imem,
		   &xs_pcie, &xs_pcie_g4x1,
		   &qsm_cfg_center, &qsm_cfg_east,
		   &qsm_cfg_mm, &qsm_cfg_north,
		   &qsm_cfg_south, &qsm_cfg_southwest,
		   &qhs_ahb2phy0, &qhs_boot_rom,
		   &qhs_camera_cfg, &qhs_clk_ctl,
		   &qhs_crypto_cfg, &qhs_eva_cfg },
};

static struct qcom_icc_bcm bcm_cn1 = {
	.name = "CN1",
	.num_nodes = 1,
	.nodes = { &qhs_display_cfg },
};

static struct qcom_icc_bcm bcm_co0 = {
	.name = "CO0",
	.enable_mask = BIT(0),
	.num_nodes = 2,
	.nodes = { &qnm_nsp, &qns_nsp_gemnoc },
};

static struct qcom_icc_bcm bcm_de0 = {
	.name = "DE0",
	.enable_mask = BIT(0),
	.num_nodes = 1,
	.nodes = { &ddr_eff_veto_slave },
};

static struct qcom_icc_bcm bcm_lp0 = {
	.name = "LP0",
	.num_nodes = 5,
	.nodes = { &qnm_lpiaon_noc_llclpi_noc, &qns_lb,
		   &qnm_lpass_lpinoc, &qns_llc_lpinoc,
		   &qns_lpass_aggnoc },
};

static struct qcom_icc_bcm bcm_mc0 = {
	.name = "MC0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_mc5 = {
	.name = "MC5",
	.num_nodes = 1,
	.nodes = { &ddr_rt_slave },
};

static struct qcom_icc_bcm bcm_mm0 = {
	.name = "MM0",
	.num_nodes = 1,
	.nodes = { &qns_mem_noc_hf },
};

static struct qcom_icc_bcm bcm_mm1 = {
	.name = "MM1",
	.enable_mask = BIT(0),
	.num_nodes = 9,
	.nodes = { &qnm_camnoc_hf, &qnm_camnoc_nrt_icp_sf,
		   &qnm_camnoc_rt_cdm_sf, &qnm_camnoc_sf,
		   &qnm_vapss_hcp, &qnm_video_cv_cpu,
		   &qnm_video_mvp, &qnm_video_v_cpu,
		   &qns_mem_noc_sf },
};

static struct qcom_icc_bcm bcm_qpc0 = {
	.name = "QPC0",
	.num_nodes = 1,
	.nodes = { &qnm_qpace },
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.keepalive = true,
	.vote_scale = 1,
	.num_nodes = 1,
	.nodes = { &qup0_core_slave },
};

static struct qcom_icc_bcm bcm_qup1 = {
	.name = "QUP1",
	.keepalive = true,
	.vote_scale = 1,
	.num_nodes = 1,
	.nodes = { &qup1_core_slave },
};

static struct qcom_icc_bcm bcm_qup2 = {
	.name = "QUP2",
	.keepalive = true,
	.vote_scale = 1,
	.num_nodes = 1,
	.nodes = { &qup2_core_slave },
};

static struct qcom_icc_bcm bcm_qup3 = {
	.name = "QUP3",
	.keepalive = true,
	.vote_scale = 1,
	.num_nodes = 1,
	.nodes = { &qup3_core_slave },
};

static struct qcom_icc_bcm bcm_qup4 = {
	.name = "QUP4",
	.keepalive = true,
	.vote_scale = 1,
	.num_nodes = 1,
	.nodes = { &qup4_core_slave },
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
	.num_nodes = 15,
	.nodes = { &alm_gic, &alm_gpu_tcu,
		   &alm_sys_tcu, &chm_apps,
		   &qnm_gpu, &qnm_lpass_gemnoc,
		   &qnm_mdsp, &qnm_mnoc_hf,
		   &qnm_mnoc_sf, &qnm_nsp_gemnoc,
		   &qnm_pcie, &qnm_snoc_sf,
		   &qnm_wlan_q6, &qns_gem_noc_cnoc,
		   &qns_pcie },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.num_nodes = 1,
	.nodes = { &qnm_aggre_noc },
};

static struct qcom_icc_bcm bcm_sn3 = {
	.name = "SN3",
	.num_nodes = 1,
	.nodes = { &qns_pcie_gemnoc },
};

static struct qcom_icc_bcm * const aggre1_noc_bcms[] = {
	&bcm_ce0,
};

static struct qcom_icc_node * const aggre1_noc_nodes[] = {
	[MASTER_QSPI_0] = &qhm_qspi,
	[MASTER_QUP_2] = &qhm_qup2,
	[MASTER_QUP_3] = &qhm_qup3,
	[MASTER_QUP_4] = &qhm_qup4,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_QUP_1] = &qxm_qup1,
	[MASTER_SOCCP_PROC] = &qxm_soccp,
	[MASTER_QDSS_ETR] = &xm_qdss_etr_0,
	[MASTER_QDSS_ETR_1] = &xm_qdss_etr_1,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_SDCC_4] = &xm_sdc4,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_USB3] = &xm_usb3,
	[SLAVE_A1NOC_SNOC] = &qns_a1noc_snoc,
};

static const struct regmap_config hawi_aggre1_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x54400,
	.fast_io = true,
};

static const struct qcom_icc_desc hawi_aggre1_noc = {
	.config = &hawi_aggre1_noc_regmap_config,
	.nodes = aggre1_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre1_noc_nodes),
	.bcms = aggre1_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_noc_bcms),
	.qos_requires_clocks = true,
};

static struct qcom_icc_bcm * const clk_virt_bcms[] = {
	&bcm_de0,
	&bcm_qup0,
	&bcm_qup1,
	&bcm_qup2,
	&bcm_qup3,
	&bcm_qup4,
};

static struct qcom_icc_node * const clk_virt_nodes[] = {
	[MASTER_DDR_EFF_VETO] = &ddr_eff_veto_master,
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[MASTER_QUP_CORE_1] = &qup1_core_master,
	[MASTER_QUP_CORE_2] = &qup2_core_master,
	[MASTER_QUP_CORE_3] = &qup3_core_master,
	[MASTER_QUP_CORE_4] = &qup4_core_master,
	[SLAVE_DDR_EFF_VETO] = &ddr_eff_veto_slave,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[SLAVE_QUP_CORE_1] = &qup1_core_slave,
	[SLAVE_QUP_CORE_2] = &qup2_core_slave,
	[SLAVE_QUP_CORE_3] = &qup3_core_slave,
	[SLAVE_QUP_CORE_4] = &qup4_core_slave,
};

static const struct qcom_icc_desc hawi_clk_virt = {
	.nodes = clk_virt_nodes,
	.num_nodes = ARRAY_SIZE(clk_virt_nodes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
};

static struct qcom_icc_bcm * const cnoc_main_bcms[] = {
	&bcm_cn0,
};

static struct qcom_icc_node * const cnoc_main_nodes[] = {
	[MASTER_GEM_NOC_CNOC] = &qnm_gemnoc_cnoc,
	[MASTER_GEM_NOC_PCIE_SNOC] = &qnm_gemnoc_pcie,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_IPC_ROUTER_FENCE] = &qhs_ipc_router_fence,
	[SLAVE_SOCCP] = &qhs_soccp,
	[SLAVE_TME_CFG] = &qhs_tme_cfg,
	[SLAVE_CNOC_CFG] = &qss_cfg,
	[SLAVE_DDRSS_CFG] = &qss_ddrss_cfg,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_PCIE_0] = &xs_pcie,
	[SLAVE_PCIE_1] = &xs_pcie_g4x1,
};

static const struct regmap_config hawi_cnoc_main_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x20000,
	.fast_io = true,
};

static const struct qcom_icc_desc hawi_cnoc_main = {
	.config = &hawi_cnoc_main_regmap_config,
	.nodes = cnoc_main_nodes,
	.num_nodes = ARRAY_SIZE(cnoc_main_nodes),
	.bcms = cnoc_main_bcms,
	.num_bcms = ARRAY_SIZE(cnoc_main_bcms),
};

static struct qcom_icc_bcm * const gem_noc_bcms[] = {
	&bcm_qpc0,
	&bcm_sh0,
	&bcm_sh1,
};

static struct qcom_icc_node * const gem_noc_nodes[] = {
	[MASTER_GIC] = &alm_gic,
	[MASTER_GPU_TCU] = &alm_gpu_tcu,
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_APPSS_PROC] = &chm_apps,
	[MASTER_GFX3D] = &qnm_gpu,
	[MASTER_LPASS_GEM_NOC] = &qnm_lpass_gemnoc,
	[MASTER_MSS_PROC] = &qnm_mdsp,
	[MASTER_MNOC_HF_MEM_NOC] = &qnm_mnoc_hf,
	[MASTER_MNOC_SF_MEM_NOC] = &qnm_mnoc_sf,
	[MASTER_COMPUTE_NOC] = &qnm_nsp_gemnoc,
	[MASTER_ANOC_PCIE_GEM_NOC] = &qnm_pcie,
	[MASTER_QPACE] = &qnm_qpace,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[MASTER_WLAN_Q6] = &qnm_wlan_q6,
	[SLAVE_GEM_NOC_CNOC] = &qns_gem_noc_cnoc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_MEM_NOC_PCIE_SNOC] = &qns_pcie,
};

static const struct regmap_config hawi_gem_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x160200,
	.fast_io = true,
};

static const struct qcom_icc_desc hawi_gem_noc = {
	.config = &hawi_gem_noc_regmap_config,
	.nodes = gem_noc_nodes,
	.num_nodes = ARRAY_SIZE(gem_noc_nodes),
	.bcms = gem_noc_bcms,
	.num_bcms = ARRAY_SIZE(gem_noc_bcms),
};

static struct qcom_icc_bcm * const llclpi_noc_bcms[] = {
	&bcm_lp0,
};

static struct qcom_icc_node * const llclpi_noc_nodes[] = {
	[MASTER_LPIAON_NOC_LLCLPI_NOC] = &qnm_lpiaon_noc_llclpi_noc,
	[SLAVE_LPASS_LPI_CC] = &qhs_lpi_cc,
	[SLAVE_LLCC_ISLAND] = &qns_lb,
	[SLAVE_SERVICE_LLCLPI_NOC] = &srvc_llclpi_noc,
	[SLAVE_SERVICE_LLCLPI_NOC_CHIPCX] = &srvc_llclpi_noc_chipcx,
};

static const struct regmap_config hawi_llclpi_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x17200,
	.fast_io = true,
};

static const struct qcom_icc_desc hawi_llclpi_noc = {
	.config = &hawi_llclpi_noc_regmap_config,
	.nodes = llclpi_noc_nodes,
	.num_nodes = ARRAY_SIZE(llclpi_noc_nodes),
	.bcms = llclpi_noc_bcms,
	.num_bcms = ARRAY_SIZE(llclpi_noc_bcms),
};

static struct qcom_icc_node * const lpass_ag_noc_nodes[] = {
	[MASTER_LPIAON_NOC] = &qnm_lpiaon_noc,
	[SLAVE_LPASS_GEM_NOC] = &qns_lpass_ag_noc_gemnoc,
};

static const struct regmap_config hawi_lpass_ag_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xc080,
	.fast_io = true,
};

static const struct qcom_icc_desc hawi_lpass_ag_noc = {
	.config = &hawi_lpass_ag_noc_regmap_config,
	.nodes = lpass_ag_noc_nodes,
	.num_nodes = ARRAY_SIZE(lpass_ag_noc_nodes),
};

static struct qcom_icc_bcm * const lpass_lpiaon_noc_bcms[] = {
	&bcm_lp0,
};

static struct qcom_icc_node * const lpass_lpiaon_noc_nodes[] = {
	[MASTER_LPASS_LPINOC] = &qnm_lpass_lpinoc,
	[SLAVE_LPIAON_NOC_LLCLPI_NOC] = &qns_llc_lpinoc,
	[SLAVE_LPIAON_NOC_LPASS_AG_NOC] = &qns_lpass_aggnoc,
};

static const struct regmap_config hawi_lpass_lpiaon_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x19080,
	.fast_io = true,
};

static const struct qcom_icc_desc hawi_lpass_lpiaon_noc = {
	.config = &hawi_lpass_lpiaon_noc_regmap_config,
	.nodes = lpass_lpiaon_noc_nodes,
	.num_nodes = ARRAY_SIZE(lpass_lpiaon_noc_nodes),
	.bcms = lpass_lpiaon_noc_bcms,
	.num_bcms = ARRAY_SIZE(lpass_lpiaon_noc_bcms),
};

static struct qcom_icc_node * const lpass_lpicx_noc_nodes[] = {
	[MASTER_LPASS_PROC] = &qnm_lpinoc_dsp_qns4m,
	[SLAVE_LPICX_NOC_LPIAON_NOC] = &qns_lpi_aon_noc,
};

static const struct regmap_config hawi_lpass_lpicx_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x46080,
	.fast_io = true,
};

static const struct qcom_icc_desc hawi_lpass_lpicx_noc = {
	.config = &hawi_lpass_lpicx_noc_regmap_config,
	.nodes = lpass_lpicx_noc_nodes,
	.num_nodes = ARRAY_SIZE(lpass_lpicx_noc_nodes),
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
	&bcm_mc5,
};

static struct qcom_icc_node * const mc_virt_nodes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[MASTER_DDR_RT] = &ddr_rt_mc,
	[SLAVE_EBI1] = &ebi,
	[SLAVE_DDR_RT] = &ddr_rt_slave,
};

static const struct qcom_icc_desc hawi_mc_virt = {
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
	[MASTER_MDP] = &qnm_mdp,
	[MASTER_MDSS_DCP] = &qnm_mdss_dcp,
	[MASTER_CDSP_HCP] = &qnm_vapss_hcp,
	[MASTER_VIDEO_CV_PROC] = &qnm_video_cv_cpu,
	[MASTER_VIDEO_EVA] = &qnm_video_eva,
	[MASTER_VIDEO_MVP] = &qnm_video_mvp,
	[MASTER_VIDEO_V_PROC] = &qnm_video_v_cpu,
	[SLAVE_MNOC_HF_MEM_NOC] = &qns_mem_noc_hf,
	[SLAVE_MNOC_SF_MEM_NOC] = &qns_mem_noc_sf,
};

static const struct regmap_config hawi_mmss_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x5f800,
	.fast_io = true,
};

static const struct qcom_icc_desc hawi_mmss_noc = {
	.config = &hawi_mmss_noc_regmap_config,
	.nodes = mmss_noc_nodes,
	.num_nodes = ARRAY_SIZE(mmss_noc_nodes),
	.bcms = mmss_noc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_noc_bcms),
};

static struct qcom_icc_bcm * const nsp_noc_bcms[] = {
	&bcm_co0,
};

static struct qcom_icc_node * const nsp_noc_nodes[] = {
	[MASTER_CDSP_PROC] = &qnm_nsp,
	[SLAVE_CDSP_MEM_NOC] = &qns_nsp_gemnoc,
};

static const struct regmap_config hawi_nsp_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x21280,
	.fast_io = true,
};

static const struct qcom_icc_desc hawi_nsp_noc = {
	.config = &hawi_nsp_noc_regmap_config,
	.nodes = nsp_noc_nodes,
	.num_nodes = ARRAY_SIZE(nsp_noc_nodes),
	.bcms = nsp_noc_bcms,
	.num_bcms = ARRAY_SIZE(nsp_noc_bcms),
};

static struct qcom_icc_bcm * const pcie_anoc_bcms[] = {
	&bcm_sn3,
};

static struct qcom_icc_node * const pcie_anoc_nodes[] = {
	[MASTER_PCIE_ANOC_CFG] = &qsm_pcie_anoc_cfg,
	[MASTER_PCIE_0] = &xm_pcie,
	[MASTER_PCIE_1] = &xm_pcie_g4x1,
	[SLAVE_ANOC_PCIE_GEM_NOC] = &qns_pcie_gemnoc,
	[SLAVE_SERVICE_PCIE_ANOC] = &srvc_pcie_aggre_noc,
};

static const struct regmap_config hawi_pcie_anoc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x12400,
	.fast_io = true,
};

static const struct qcom_icc_desc hawi_pcie_anoc = {
	.config = &hawi_pcie_anoc_regmap_config,
	.nodes = pcie_anoc_nodes,
	.num_nodes = ARRAY_SIZE(pcie_anoc_nodes),
	.bcms = pcie_anoc_bcms,
	.num_bcms = ARRAY_SIZE(pcie_anoc_bcms),
	.qos_requires_clocks = true,
};

static struct qcom_icc_bcm * const stdst_cfg_bcms[] = {
	&bcm_cn0,
	&bcm_cn1,
};

static struct qcom_icc_node * const stdst_cfg_nodes[] = {
	[MASTER_CFG_CENTER] = &qsm_cfg_center,
	[MASTER_CFG_EAST] = &qsm_cfg_east,
	[MASTER_CFG_MM] = &qsm_cfg_mm,
	[MASTER_CFG_NORTH] = &qsm_cfg_north,
	[MASTER_CFG_SOUTH] = &qsm_cfg_south,
	[MASTER_CFG_SOUTHWEST] = &qsm_cfg_southwest,
	[SLAVE_AHB2PHY_SOUTH] = &qhs_ahb2phy0,
	[SLAVE_BOOT_ROM] = &qhs_boot_rom,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CRYPTO_CFG] = &qhs_crypto_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_EVA_CFG] = &qhs_eva_cfg,
	[SLAVE_GFX3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_I2C] = &qhs_i2c,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPC_ROUTER_CFG] = &qhs_ipc_router,
	[SLAVE_IRIS_CFG] = &qhs_iris_cfg,
	[SLAVE_CNOC_MSS] = &qhs_mss_cfg,
	[SLAVE_PCIE_0_CFG] = &qhs_pcie_cfg,
	[SLAVE_PCIE_1_CFG] = &qhs_pcie_g4x1_cfg,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QSPI_0] = &qhs_qspi,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_QUP_2] = &qhs_qup2,
	[SLAVE_QUP_3] = &qhs_qup3,
	[SLAVE_QUP_4] = &qhs_qup4,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SDCC_4] = &qhs_sdc4,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB3] = &qhs_usb3,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_PCIE_ANOC_CFG] = &qss_pcie_anoc_cfg,
	[SLAVE_QDSS_CFG] = &qss_qdss_cfg,
	[SLAVE_QDSS_STM] = &qss_qdss_stm,
	[SLAVE_TCSR] = &qss_tcsr,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct regmap_config hawi_stdst_cfg_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xb1000,
	.fast_io = true,
};

static const struct qcom_icc_desc hawi_stdst_cfg = {
	.config = &hawi_stdst_cfg_regmap_config,
	.nodes = stdst_cfg_nodes,
	.num_nodes = ARRAY_SIZE(stdst_cfg_nodes),
	.bcms = stdst_cfg_bcms,
	.num_bcms = ARRAY_SIZE(stdst_cfg_bcms),
};

static struct qcom_icc_node * const stdst_main_nodes[] = {
	[MASTER_CNOC_STARDUST] = &qsm_cnoc_main,
	[SLAVE_STARDUST_CENTER_CFG] = &qss_stdst_center_cfg,
	[SLAVE_STARDUST_EAST_CFG] = &qss_stdst_east_cfg,
	[SLAVE_STARDUST_MM_CFG] = &qss_stdst_mm_cfg,
	[SLAVE_STARDUST_NORTH_CFG] = &qss_stdst_north_cfg,
	[SLAVE_STARDUST_SOUTH_CFG] = &qss_stdst_south_cfg,
	[SLAVE_STARDUST_SOUTHWEST_CFG] = &qss_stdst_southwest_cfg,
};

static const struct regmap_config hawi_stdst_main_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x10000,
	.fast_io = true,
};

static const struct qcom_icc_desc hawi_stdst_main = {
	.config = &hawi_stdst_main_regmap_config,
	.nodes = stdst_main_nodes,
	.num_nodes = ARRAY_SIZE(stdst_main_nodes),
};

static struct qcom_icc_bcm * const system_noc_bcms[] = {
	&bcm_sn0,
	&bcm_sn2,
};

static struct qcom_icc_node * const system_noc_nodes[] = {
	[MASTER_A1NOC_SNOC] = &qnm_aggre_noc,
	[MASTER_APSS_NOC] = &qnm_apss_noc,
	[MASTER_CNOC_SNOC] = &qnm_cnoc_data,
	[SLAVE_SNOC_GEM_NOC_SF] = &qns_gemnoc_sf,
};

static const struct regmap_config hawi_system_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x20080,
	.fast_io = true,
};

static const struct qcom_icc_desc hawi_system_noc = {
	.config = &hawi_system_noc_regmap_config,
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,hawi-aggre1-noc", .data = &hawi_aggre1_noc },
	{ .compatible = "qcom,hawi-clk-virt", .data = &hawi_clk_virt },
	{ .compatible = "qcom,hawi-cnoc-main", .data = &hawi_cnoc_main },
	{ .compatible = "qcom,hawi-gem-noc", .data = &hawi_gem_noc },
	{ .compatible = "qcom,hawi-llclpi-noc", .data = &hawi_llclpi_noc },
	{ .compatible = "qcom,hawi-lpass-ag-noc", .data = &hawi_lpass_ag_noc },
	{ .compatible = "qcom,hawi-lpass-lpiaon-noc", .data = &hawi_lpass_lpiaon_noc },
	{ .compatible = "qcom,hawi-lpass-lpicx-noc", .data = &hawi_lpass_lpicx_noc },
	{ .compatible = "qcom,hawi-mc-virt", .data = &hawi_mc_virt },
	{ .compatible = "qcom,hawi-mmss-noc", .data = &hawi_mmss_noc },
	{ .compatible = "qcom,hawi-nsp-noc", .data = &hawi_nsp_noc },
	{ .compatible = "qcom,hawi-pcie-anoc", .data = &hawi_pcie_anoc },
	{ .compatible = "qcom,hawi-stdst-cfg", .data = &hawi_stdst_cfg },
	{ .compatible = "qcom,hawi-stdst-main", .data = &hawi_stdst_main },
	{ .compatible = "qcom,hawi-system-noc", .data = &hawi_system_noc },
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-hawi",
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

MODULE_DESCRIPTION("Qualcomm Hawi NoC driver");
MODULE_LICENSE("GPL");
