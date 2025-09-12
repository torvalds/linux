// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <dt-bindings/interconnect/qcom,glymur-rpmh.h>

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

static struct qcom_icc_node qhs_av1_enc_cfg = {
	.name = "qhs_av1_enc_cfg",
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

static struct qcom_icc_node qhs_display_cfg = {
	.name = "qhs_display_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
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

static struct qcom_icc_node qhs_pcie3a_cfg = {
	.name = "qhs_pcie3a_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie3b_cfg = {
	.name = "qhs_pcie3b_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie4_cfg = {
	.name = "qhs_pcie4_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie5_cfg = {
	.name = "qhs_pcie5_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie6_cfg = {
	.name = "qhs_pcie6_cfg",
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

static struct qcom_icc_node qhs_smmuv3_cfg = {
	.name = "qhs_smmuv3_cfg",
	.channels = 1,
	.buswidth = 8,
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

static struct qcom_icc_node qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb2_0_cfg = {
	.name = "qhs_usb2_0_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb3_0_cfg = {
	.name = "qhs_usb3_0_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb3_1_cfg = {
	.name = "qhs_usb3_1_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb3_2_cfg = {
	.name = "qhs_usb3_2_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb3_mp_cfg = {
	.name = "qhs_usb3_mp_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb4_0_cfg = {
	.name = "qhs_usb4_0_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb4_1_cfg = {
	.name = "qhs_usb4_1_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb4_2_cfg = {
	.name = "qhs_usb4_2_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qss_lpass_qtb_cfg = {
	.name = "qss_lpass_qtb_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qss_nsp_qtb_cfg = {
	.name = "qss_nsp_qtb_cfg",
	.channels = 1,
	.buswidth = 4,
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

static struct qcom_icc_node qhs_aoss = {
	.name = "qhs_aoss",
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

static struct qcom_icc_node qns_apss = {
	.name = "qns_apss",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qxs_boot_imem = {
	.name = "qxs_boot_imem",
	.channels = 1,
	.buswidth = 16,
};

static struct qcom_icc_node qxs_imem = {
	.name = "qxs_imem",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.channels = 12,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_mnoc = {
	.name = "srvc_mnoc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_nsinoc = {
	.name = "srvc_nsinoc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_pcie_east_aggre_noc = {
	.name = "srvc_pcie_east_aggre_noc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_hscnoc_pcie_east_ms_mpu_cfg = {
	.name = "qhs_hscnoc_pcie_east_ms_mpu_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_pcie_east = {
	.name = "srvc_pcie_east",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node xs_pcie_0 = {
	.name = "xs_pcie_0",
	.channels = 1,
	.buswidth = 16,
};

static struct qcom_icc_node xs_pcie_1 = {
	.name = "xs_pcie_1",
	.channels = 1,
	.buswidth = 32,
};

static struct qcom_icc_node xs_pcie_5 = {
	.name = "xs_pcie_5",
	.channels = 1,
	.buswidth = 32,
};

static struct qcom_icc_node srvc_pcie_west_aggre_noc = {
	.name = "srvc_pcie_west_aggre_noc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_hscnoc_pcie_west_ms_mpu_cfg = {
	.name = "qhs_hscnoc_pcie_west_ms_mpu_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_pcie_west = {
	.name = "srvc_pcie_west",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node xs_pcie_2 = {
	.name = "xs_pcie_2",
	.channels = 1,
	.buswidth = 16,
};

static struct qcom_icc_node xs_pcie_3a = {
	.name = "xs_pcie_3a",
	.channels = 1,
	.buswidth = 64,
};

static struct qcom_icc_node xs_pcie_3b = {
	.name = "xs_pcie_3b",
	.channels = 1,
	.buswidth = 32,
};

static struct qcom_icc_node xs_pcie_4 = {
	.name = "xs_pcie_4",
	.channels = 1,
	.buswidth = 16,
};

static struct qcom_icc_node xs_pcie_6 = {
	.name = "xs_pcie_6",
	.channels = 1,
	.buswidth = 16,
};

static struct qcom_icc_node qup0_core_master = {
	.name = "qup0_core_master",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qup0_core_slave },
};

static struct qcom_icc_node qup1_core_master = {
	.name = "qup1_core_master",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qup1_core_slave },
};

static struct qcom_icc_node qup2_core_master = {
	.name = "qup2_core_master",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qup2_core_slave },
};

static struct qcom_icc_node llcc_mc = {
	.name = "llcc_mc",
	.channels = 12,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &ebi },
};

static struct qcom_icc_node qsm_mnoc_cfg = {
	.name = "qsm_mnoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &srvc_mnoc },
};

static struct qcom_icc_node qsm_pcie_east_anoc_cfg = {
	.name = "qsm_pcie_east_anoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &srvc_pcie_east_aggre_noc },
};

static struct qcom_icc_node qnm_hscnoc_pcie_east = {
	.name = "qnm_hscnoc_pcie_east",
	.channels = 1,
	.buswidth = 32,
	.num_links = 3,
	.link_nodes = (struct qcom_icc_node *[]) { &xs_pcie_0, &xs_pcie_1,
		      &xs_pcie_5 },
};

static struct qcom_icc_node qsm_cnoc_pcie_east_slave_cfg = {
	.name = "qsm_cnoc_pcie_east_slave_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.link_nodes = (struct qcom_icc_node *[]) { &qhs_hscnoc_pcie_east_ms_mpu_cfg,
		      &srvc_pcie_east },
};

static struct qcom_icc_node qsm_pcie_west_anoc_cfg = {
	.name = "qsm_pcie_west_anoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &srvc_pcie_west_aggre_noc },
};

static struct qcom_icc_node qnm_hscnoc_pcie_west = {
	.name = "qnm_hscnoc_pcie_west",
	.channels = 1,
	.buswidth = 32,
	.num_links = 5,
	.link_nodes = (struct qcom_icc_node *[]) { &xs_pcie_2, &xs_pcie_3a,
		      &xs_pcie_3b, &xs_pcie_4,
		      &xs_pcie_6 },
};

static struct qcom_icc_node qsm_cnoc_pcie_west_slave_cfg = {
	.name = "qsm_cnoc_pcie_west_slave_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.link_nodes = (struct qcom_icc_node *[]) { &qhs_hscnoc_pcie_west_ms_mpu_cfg,
		      &srvc_pcie_west },
};

static struct qcom_icc_node qss_cnoc_pcie_slave_east_cfg = {
	.name = "qss_cnoc_pcie_slave_east_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qsm_cnoc_pcie_east_slave_cfg },
};

static struct qcom_icc_node qss_cnoc_pcie_slave_west_cfg = {
	.name = "qss_cnoc_pcie_slave_west_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qsm_cnoc_pcie_west_slave_cfg },
};

static struct qcom_icc_node qss_mnoc_cfg = {
	.name = "qss_mnoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qsm_mnoc_cfg },
};

static struct qcom_icc_node qss_pcie_east_anoc_cfg = {
	.name = "qss_pcie_east_anoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qsm_pcie_east_anoc_cfg },
};

static struct qcom_icc_node qss_pcie_west_anoc_cfg = {
	.name = "qss_pcie_west_anoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qsm_pcie_west_anoc_cfg },
};

static struct qcom_icc_node qns_llcc = {
	.name = "qns_llcc",
	.channels = 12,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &llcc_mc },
};

static struct qcom_icc_node qns_pcie_east = {
	.name = "qns_pcie_east",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_hscnoc_pcie_east },
};

static struct qcom_icc_node qns_pcie_west = {
	.name = "qns_pcie_west",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_hscnoc_pcie_west },
};

static struct qcom_icc_node qsm_cfg = {
	.name = "qsm_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 51,
	.link_nodes = (struct qcom_icc_node *[]) { &qhs_ahb2phy0, &qhs_ahb2phy1,
		      &qhs_ahb2phy2, &qhs_ahb2phy3,
		      &qhs_av1_enc_cfg, &qhs_camera_cfg,
		      &qhs_clk_ctl, &qhs_crypto0_cfg,
		      &qhs_display_cfg, &qhs_gpuss_cfg,
		      &qhs_imem_cfg, &qhs_pcie0_cfg,
		      &qhs_pcie1_cfg, &qhs_pcie2_cfg,
		      &qhs_pcie3a_cfg, &qhs_pcie3b_cfg,
		      &qhs_pcie4_cfg, &qhs_pcie5_cfg,
		      &qhs_pcie6_cfg, &qhs_pcie_rscc,
		      &qhs_pdm, &qhs_prng,
		      &qhs_qdss_cfg, &qhs_qspi,
		      &qhs_qup0, &qhs_qup1,
		      &qhs_qup2, &qhs_sdc2,
		      &qhs_sdc4, &qhs_smmuv3_cfg,
		      &qhs_tcsr, &qhs_tlmm,
		      &qhs_ufs_mem_cfg, &qhs_usb2_0_cfg,
		      &qhs_usb3_0_cfg, &qhs_usb3_1_cfg,
		      &qhs_usb3_2_cfg, &qhs_usb3_mp_cfg,
		      &qhs_usb4_0_cfg, &qhs_usb4_1_cfg,
		      &qhs_usb4_2_cfg, &qhs_venus_cfg,
		      &qss_cnoc_pcie_slave_east_cfg, &qss_cnoc_pcie_slave_west_cfg,
		      &qss_lpass_qtb_cfg, &qss_mnoc_cfg,
		      &qss_nsp_qtb_cfg, &qss_pcie_east_anoc_cfg,
		      &qss_pcie_west_anoc_cfg, &xs_qdss_stm,
		      &xs_sys_tcu_cfg },
};

static struct qcom_icc_node xm_gic = {
	.name = "xm_gic",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x33000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_llcc },
};

static struct qcom_icc_node qss_cfg = {
	.name = "qss_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qsm_cfg },
};

static struct qcom_icc_node qnm_hscnoc_cnoc = {
	.name = "qnm_hscnoc_cnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 8,
	.link_nodes = (struct qcom_icc_node *[]) { &qhs_aoss, &qhs_ipc_router,
		      &qhs_soccp, &qhs_tme_cfg,
		      &qns_apss, &qss_cfg,
		      &qxs_boot_imem, &qxs_imem },
};

static struct qcom_icc_node qns_hscnoc_cnoc = {
	.name = "qns_hscnoc_cnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_hscnoc_cnoc },
};

static struct qcom_icc_node alm_gpu_tcu = {
	.name = "alm_gpu_tcu",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x933000 },
		.prio = 1,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 2,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_hscnoc_cnoc, &qns_llcc },
};

static struct qcom_icc_node alm_pcie_qtc = {
	.name = "alm_pcie_qtc",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x51f000 },
		.prio = 3,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 2,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_hscnoc_cnoc, &qns_llcc },
};

static struct qcom_icc_node alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x51f080 },
		.prio = 6,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 2,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_hscnoc_cnoc, &qns_llcc },
};

static struct qcom_icc_node chm_apps = {
	.name = "chm_apps",
	.channels = 6,
	.buswidth = 32,
	.num_links = 4,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_hscnoc_cnoc, &qns_llcc,
		      &qns_pcie_east, &qns_pcie_west },
};

static struct qcom_icc_node qnm_aggre_noc_east = {
	.name = "qnm_aggre_noc_east",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x934000 },
		.prio = 2,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 4,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_hscnoc_cnoc, &qns_llcc,
		      &qns_pcie_east, &qns_pcie_west },
};

static struct qcom_icc_node qnm_gpu = {
	.name = "qnm_gpu",
	.channels = 4,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 4,
		.port_offsets = { 0x935000, 0x936000, 0x937000, 0x938000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 4,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_hscnoc_cnoc, &qns_llcc,
		      &qns_pcie_east, &qns_pcie_west },
};

static struct qcom_icc_node qnm_lpass = {
	.name = "qnm_lpass",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x939000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 4,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_hscnoc_cnoc, &qns_llcc,
		      &qns_pcie_east, &qns_pcie_west },
};

static struct qcom_icc_node qnm_mnoc_hf = {
	.name = "qnm_mnoc_hf",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x721000, 0x721080 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 4,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_hscnoc_cnoc, &qns_llcc,
		      &qns_pcie_east, &qns_pcie_west },
};

static struct qcom_icc_node qnm_mnoc_sf = {
	.name = "qnm_mnoc_sf",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x721100, 0x721180 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 4,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_hscnoc_cnoc, &qns_llcc,
		      &qns_pcie_east, &qns_pcie_west },
};

static struct qcom_icc_node qnm_nsp_noc = {
	.name = "qnm_nsp_noc",
	.channels = 4,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 4,
		.port_offsets = { 0x816000, 0x816080, 0x816100, 0x816180 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 4,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_hscnoc_cnoc, &qns_llcc,
		      &qns_pcie_east, &qns_pcie_west },
};

static struct qcom_icc_node qnm_pcie_east = {
	.name = "qnm_pcie_east",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x93a000 },
		.prio = 2,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 2,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_hscnoc_cnoc, &qns_llcc },
};

static struct qcom_icc_node qnm_pcie_west = {
	.name = "qnm_pcie_west",
	.channels = 1,
	.buswidth = 64,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x721200 },
		.prio = 2,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 2,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_hscnoc_cnoc, &qns_llcc },
};

static struct qcom_icc_node qnm_snoc_sf = {
	.name = "qnm_snoc_sf",
	.channels = 1,
	.buswidth = 64,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x51f100 },
		.prio = 2,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 4,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_hscnoc_cnoc, &qns_llcc,
		      &qns_pcie_east, &qns_pcie_west },
};

static struct qcom_icc_node qxm_wlan_q6 = {
	.name = "qxm_wlan_q6",
	.channels = 1,
	.buswidth = 8,
	.num_links = 4,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_hscnoc_cnoc, &qns_llcc,
		      &qns_pcie_east, &qns_pcie_west },
};

static struct qcom_icc_node qns_a4noc_hscnoc = {
	.name = "qns_a4noc_hscnoc",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_aggre_noc_east },
};

static struct qcom_icc_node qns_lpass_ag_noc_gemnoc = {
	.name = "qns_lpass_ag_noc_gemnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_lpass },
};

static struct qcom_icc_node qns_mem_noc_hf = {
	.name = "qns_mem_noc_hf",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_mnoc_hf },
};

static struct qcom_icc_node qns_mem_noc_sf = {
	.name = "qns_mem_noc_sf",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_mnoc_sf },
};

static struct qcom_icc_node qns_nsp_hscnoc = {
	.name = "qns_nsp_hscnoc",
	.channels = 4,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_nsp_noc },
};

static struct qcom_icc_node qns_pcie_east_mem_noc = {
	.name = "qns_pcie_east_mem_noc",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_pcie_east },
};

static struct qcom_icc_node qns_pcie_west_mem_noc = {
	.name = "qns_pcie_west_mem_noc",
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_pcie_west },
};

static struct qcom_icc_node qns_gemnoc_sf = {
	.name = "qns_gemnoc_sf",
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_snoc_sf },
};

static struct qcom_icc_node xm_usb3_0 = {
	.name = "xm_usb3_0",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xa000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a4noc_hscnoc },
};

static struct qcom_icc_node xm_usb3_1 = {
	.name = "xm_usb3_1",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xb000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a4noc_hscnoc },
};

static struct qcom_icc_node xm_usb4_0 = {
	.name = "xm_usb4_0",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xc000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a4noc_hscnoc },
};

static struct qcom_icc_node xm_usb4_1 = {
	.name = "xm_usb4_1",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xd000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a4noc_hscnoc },
};

static struct qcom_icc_node qnm_lpiaon_noc = {
	.name = "qnm_lpiaon_noc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_lpass_ag_noc_gemnoc },
};

static struct qcom_icc_node qnm_av1_enc = {
	.name = "qnm_av1_enc",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x30000 },
		.prio = 4,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_camnoc_hf = {
	.name = "qnm_camnoc_hf",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x29000, 0x2a000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_mem_noc_hf },
};

static struct qcom_icc_node qnm_camnoc_icp = {
	.name = "qnm_camnoc_icp",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x2b000 },
		.prio = 4,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_camnoc_sf = {
	.name = "qnm_camnoc_sf",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x2c000, 0x2d000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_eva = {
	.name = "qnm_eva",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x34000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_mdp = {
	.name = "qnm_mdp",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 2,
		.port_offsets = { 0x2e000, 0x2f000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_mem_noc_hf },
};

static struct qcom_icc_node qnm_vapss_hcp = {
	.name = "qnm_vapss_hcp",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_video = {
	.name = "qnm_video",
	.channels = 4,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 4,
		.port_offsets = { 0x31000, 0x32000, 0x37000, 0x38000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_video_cv_cpu = {
	.name = "qnm_video_cv_cpu",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x33000 },
		.prio = 4,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_video_v_cpu = {
	.name = "qnm_video_v_cpu",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x35000 },
		.prio = 4,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_nsp = {
	.name = "qnm_nsp",
	.channels = 4,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_nsp_hscnoc },
};

static struct qcom_icc_node xm_pcie_0 = {
	.name = "xm_pcie_0",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xb000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_pcie_east_mem_noc },
};

static struct qcom_icc_node xm_pcie_1 = {
	.name = "xm_pcie_1",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xc000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_pcie_east_mem_noc },
};

static struct qcom_icc_node xm_pcie_5 = {
	.name = "xm_pcie_5",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xd000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_pcie_east_mem_noc },
};

static struct qcom_icc_node xm_pcie_2 = {
	.name = "xm_pcie_2",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xd000 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_pcie_west_mem_noc },
};

static struct qcom_icc_node xm_pcie_3a = {
	.name = "xm_pcie_3a",
	.channels = 1,
	.buswidth = 64,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xd200 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_pcie_west_mem_noc },
};

static struct qcom_icc_node xm_pcie_3b = {
	.name = "xm_pcie_3b",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xd400 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_pcie_west_mem_noc },
};

static struct qcom_icc_node xm_pcie_4 = {
	.name = "xm_pcie_4",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xd600 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_pcie_west_mem_noc },
};

static struct qcom_icc_node xm_pcie_6 = {
	.name = "xm_pcie_6",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xd800 },
		.prio = 2,
		.urg_fwd = 0,
		.prio_fwd_disable = 0,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_pcie_west_mem_noc },
};

static struct qcom_icc_node qnm_aggre1_noc = {
	.name = "qnm_aggre1_noc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gemnoc_sf },
};

static struct qcom_icc_node qnm_aggre2_noc = {
	.name = "qnm_aggre2_noc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gemnoc_sf },
};

static struct qcom_icc_node qnm_aggre3_noc = {
	.name = "qnm_aggre3_noc",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gemnoc_sf },
};

static struct qcom_icc_node qnm_nsi_noc = {
	.name = "qnm_nsi_noc",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x1c000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gemnoc_sf },
};

static struct qcom_icc_node qnm_oobmss = {
	.name = "qnm_oobmss",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x1b000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gemnoc_sf },
};

static struct qcom_icc_node qns_a1noc_snoc = {
	.name = "qns_a1noc_snoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_aggre1_noc },
};

static struct qcom_icc_node qns_a2noc_snoc = {
	.name = "qns_a2noc_snoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_aggre2_noc },
};

static struct qcom_icc_node qns_a3noc_snoc = {
	.name = "qns_a3noc_snoc",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_aggre3_noc },
};

static struct qcom_icc_node qns_lpass_aggnoc = {
	.name = "qns_lpass_aggnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_lpiaon_noc },
};

static struct qcom_icc_node qns_system_noc = {
	.name = "qns_system_noc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_nsi_noc },
};

static struct qcom_icc_node qns_oobmss_snoc = {
	.name = "qns_oobmss_snoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_oobmss },
};

static struct qcom_icc_node qxm_crypto = {
	.name = "qxm_crypto",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xb000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a1noc_snoc },
};

static struct qcom_icc_node qxm_soccp = {
	.name = "qxm_soccp",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xe000 },
		.prio = 0,
		.urg_fwd = 1,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a1noc_snoc },
};

static struct qcom_icc_node xm_qdss_etr_0 = {
	.name = "xm_qdss_etr_0",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xc000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a1noc_snoc },
};

static struct qcom_icc_node xm_qdss_etr_1 = {
	.name = "xm_qdss_etr_1",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xd000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a1noc_snoc },
};

static struct qcom_icc_node xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0xa000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a2noc_snoc },
};

static struct qcom_icc_node xm_usb3_2 = {
	.name = "xm_usb3_2",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x8000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a2noc_snoc },
};

static struct qcom_icc_node xm_usb4_2 = {
	.name = "xm_usb4_2",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x9000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a2noc_snoc },
};

static struct qcom_icc_node qhm_qspi = {
	.name = "qhm_qspi",
	.channels = 1,
	.buswidth = 4,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x10000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a3noc_snoc },
};

static struct qcom_icc_node qhm_qup0 = {
	.name = "qhm_qup0",
	.channels = 1,
	.buswidth = 4,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x11000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a3noc_snoc },
};

static struct qcom_icc_node qhm_qup1 = {
	.name = "qhm_qup1",
	.channels = 1,
	.buswidth = 4,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x12000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a3noc_snoc },
};

static struct qcom_icc_node qhm_qup2 = {
	.name = "qhm_qup2",
	.channels = 1,
	.buswidth = 4,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x13000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a3noc_snoc },
};

static struct qcom_icc_node qxm_sp = {
	.name = "qxm_sp",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a3noc_snoc },
};

static struct qcom_icc_node xm_sdc2 = {
	.name = "xm_sdc2",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x18000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a3noc_snoc },
};

static struct qcom_icc_node xm_sdc4 = {
	.name = "xm_sdc4",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x14000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a3noc_snoc },
};

static struct qcom_icc_node xm_usb2_0 = {
	.name = "xm_usb2_0",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x15000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a3noc_snoc },
};

static struct qcom_icc_node xm_usb3_mp = {
	.name = "xm_usb3_mp",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &(const struct qcom_icc_qosbox) {
		.num_ports = 1,
		.port_offsets = { 0x16000 },
		.prio = 0,
		.urg_fwd = 0,
		.prio_fwd_disable = 1,
	},
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a3noc_snoc },
};

static struct qcom_icc_node qnm_lpass_lpinoc = {
	.name = "qnm_lpass_lpinoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_lpass_aggnoc },
};

static struct qcom_icc_node xm_cpucp = {
	.name = "xm_cpucp",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_system_noc, &srvc_nsinoc },
};

static struct qcom_icc_node xm_mem_sp = {
	.name = "xm_mem_sp",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_oobmss_snoc },
};

static struct qcom_icc_node qns_lpi_aon_noc = {
	.name = "qns_lpi_aon_noc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_lpass_lpinoc },
};

static struct qcom_icc_node qnm_lpinoc_dsp_qns4m = {
	.name = "qnm_lpinoc_dsp_qns4m",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_lpi_aon_noc },
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
	.keepalive = true,
	.enable_mask = BIT(0),
	.num_nodes = 60,
	.nodes = { &qsm_cfg, &qhs_ahb2phy0,
		   &qhs_ahb2phy1, &qhs_ahb2phy2,
		   &qhs_ahb2phy3, &qhs_av1_enc_cfg,
		   &qhs_camera_cfg, &qhs_clk_ctl,
		   &qhs_crypto0_cfg, &qhs_gpuss_cfg,
		   &qhs_imem_cfg, &qhs_pcie0_cfg,
		   &qhs_pcie1_cfg, &qhs_pcie2_cfg,
		   &qhs_pcie3a_cfg, &qhs_pcie3b_cfg,
		   &qhs_pcie4_cfg, &qhs_pcie5_cfg,
		   &qhs_pcie6_cfg, &qhs_pcie_rscc,
		   &qhs_pdm, &qhs_prng,
		   &qhs_qdss_cfg, &qhs_qspi,
		   &qhs_qup0, &qhs_qup1,
		   &qhs_qup2, &qhs_sdc2,
		   &qhs_sdc4, &qhs_smmuv3_cfg,
		   &qhs_tcsr, &qhs_tlmm,
		   &qhs_ufs_mem_cfg, &qhs_usb2_0_cfg,
		   &qhs_usb3_0_cfg, &qhs_usb3_1_cfg,
		   &qhs_usb3_2_cfg, &qhs_usb3_mp_cfg,
		   &qhs_usb4_0_cfg, &qhs_usb4_1_cfg,
		   &qhs_usb4_2_cfg, &qhs_venus_cfg,
		   &qss_cnoc_pcie_slave_east_cfg, &qss_cnoc_pcie_slave_west_cfg,
		   &qss_lpass_qtb_cfg, &qss_mnoc_cfg,
		   &qss_nsp_qtb_cfg, &qss_pcie_east_anoc_cfg,
		   &qss_pcie_west_anoc_cfg, &xs_qdss_stm,
		   &xs_sys_tcu_cfg, &qnm_hscnoc_cnoc,
		   &qhs_aoss, &qhs_ipc_router,
		   &qhs_soccp, &qhs_tme_cfg,
		   &qns_apss, &qss_cfg,
		   &qxs_boot_imem, &qxs_imem },
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
	.nodes = { &qnm_nsp, &qns_nsp_hscnoc },
};

static struct qcom_icc_bcm bcm_lp0 = {
	.name = "LP0",
	.num_nodes = 2,
	.nodes = { &qnm_lpass_lpinoc, &qns_lpass_aggnoc },
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
	.num_nodes = 11,
	.nodes = { &qnm_av1_enc, &qnm_camnoc_hf,
		   &qnm_camnoc_icp, &qnm_camnoc_sf,
		   &qnm_eva, &qnm_mdp,
		   &qnm_vapss_hcp, &qnm_video,
		   &qnm_video_cv_cpu, &qnm_video_v_cpu,
		   &qns_mem_noc_sf },
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

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_sh1 = {
	.name = "SH1",
	.enable_mask = BIT(0),
	.num_nodes = 18,
	.nodes = { &alm_gpu_tcu, &alm_pcie_qtc,
		   &alm_sys_tcu, &chm_apps,
		   &qnm_aggre_noc_east, &qnm_gpu,
		   &qnm_lpass, &qnm_mnoc_hf,
		   &qnm_mnoc_sf, &qnm_nsp_noc,
		   &qnm_pcie_east, &qnm_pcie_west,
		   &qnm_snoc_sf, &qxm_wlan_q6,
		   &xm_gic, &qns_hscnoc_cnoc,
		   &qns_pcie_east, &qns_pcie_west },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.enable_mask = BIT(0),
	.num_nodes = 1,
	.nodes = { &qnm_oobmss },
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

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.num_nodes = 1,
	.nodes = { &qnm_aggre3_noc },
};

static struct qcom_icc_bcm bcm_sn5 = {
	.name = "SN5",
	.num_nodes = 1,
	.nodes = { &qns_a4noc_hscnoc },
};

static struct qcom_icc_bcm bcm_sn6 = {
	.name = "SN6",
	.num_nodes = 4,
	.nodes = { &qns_pcie_east_mem_noc, &qnm_hscnoc_pcie_east,
		   &qns_pcie_west_mem_noc, &qnm_hscnoc_pcie_west },
};

static struct qcom_icc_bcm * const aggre1_noc_bcms[] = {
	&bcm_ce0,
};

static struct qcom_icc_node * const aggre1_noc_nodes[] = {
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_SOCCP_PROC] = &qxm_soccp,
	[MASTER_QDSS_ETR] = &xm_qdss_etr_0,
	[MASTER_QDSS_ETR_1] = &xm_qdss_etr_1,
	[SLAVE_A1NOC_SNOC] = &qns_a1noc_snoc,
};

static const struct regmap_config glymur_aggre1_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x14400,
	.fast_io = true,
};

static const struct qcom_icc_desc glymur_aggre1_noc = {
	.config = &glymur_aggre1_noc_regmap_config,
	.nodes = aggre1_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre1_noc_nodes),
	.bcms = aggre1_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_noc_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_node * const aggre2_noc_nodes[] = {
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_USB3_2] = &xm_usb3_2,
	[MASTER_USB4_2] = &xm_usb4_2,
	[SLAVE_A2NOC_SNOC] = &qns_a2noc_snoc,
};

static const struct regmap_config glymur_aggre2_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x14400,
	.fast_io = true,
};

static const struct qcom_icc_desc glymur_aggre2_noc = {
	.config = &glymur_aggre2_noc_regmap_config,
	.nodes = aggre2_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre2_noc_nodes),
	.alloc_dyn_id = true,
	.qos_requires_clocks = true,
};

static struct qcom_icc_node * const aggre3_noc_nodes[] = {
	[MASTER_QSPI_0] = &qhm_qspi,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_QUP_1] = &qhm_qup1,
	[MASTER_QUP_2] = &qhm_qup2,
	[MASTER_SP] = &qxm_sp,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_SDCC_4] = &xm_sdc4,
	[MASTER_USB2] = &xm_usb2_0,
	[MASTER_USB3_MP] = &xm_usb3_mp,
	[SLAVE_A3NOC_SNOC] = &qns_a3noc_snoc,
};

static const struct regmap_config glymur_aggre3_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x1d400,
	.fast_io = true,
};

static const struct qcom_icc_desc glymur_aggre3_noc = {
	.config = &glymur_aggre3_noc_regmap_config,
	.nodes = aggre3_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre3_noc_nodes),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const aggre4_noc_bcms[] = {
	&bcm_sn5,
};

static struct qcom_icc_node * const aggre4_noc_nodes[] = {
	[MASTER_USB3_0] = &xm_usb3_0,
	[MASTER_USB3_1] = &xm_usb3_1,
	[MASTER_USB4_0] = &xm_usb4_0,
	[MASTER_USB4_1] = &xm_usb4_1,
	[SLAVE_A4NOC_HSCNOC] = &qns_a4noc_hscnoc,
};

static const struct regmap_config glymur_aggre4_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x14400,
	.fast_io = true,
};

static const struct qcom_icc_desc glymur_aggre4_noc = {
	.config = &glymur_aggre4_noc_regmap_config,
	.nodes = aggre4_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre4_noc_nodes),
	.bcms = aggre4_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre4_noc_bcms),
	.alloc_dyn_id = true,
	.qos_requires_clocks = true,
};

static struct qcom_icc_bcm * const clk_virt_bcms[] = {
	&bcm_qup0,
	&bcm_qup1,
	&bcm_qup2,
};

static struct qcom_icc_node * const clk_virt_nodes[] = {
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[MASTER_QUP_CORE_1] = &qup1_core_master,
	[MASTER_QUP_CORE_2] = &qup2_core_master,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[SLAVE_QUP_CORE_1] = &qup1_core_slave,
	[SLAVE_QUP_CORE_2] = &qup2_core_slave,
};

static const struct qcom_icc_desc glymur_clk_virt = {
	.nodes = clk_virt_nodes,
	.num_nodes = ARRAY_SIZE(clk_virt_nodes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const cnoc_cfg_bcms[] = {
	&bcm_cn0,
	&bcm_cn1,
};

static struct qcom_icc_node * const cnoc_cfg_nodes[] = {
	[MASTER_CNOC_CFG] = &qsm_cfg,
	[SLAVE_AHB2PHY_SOUTH] = &qhs_ahb2phy0,
	[SLAVE_AHB2PHY_NORTH] = &qhs_ahb2phy1,
	[SLAVE_AHB2PHY_2] = &qhs_ahb2phy2,
	[SLAVE_AHB2PHY_3] = &qhs_ahb2phy3,
	[SLAVE_AV1_ENC_CFG] = &qhs_av1_enc_cfg,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_GFX3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_PCIE_0_CFG] = &qhs_pcie0_cfg,
	[SLAVE_PCIE_1_CFG] = &qhs_pcie1_cfg,
	[SLAVE_PCIE_2_CFG] = &qhs_pcie2_cfg,
	[SLAVE_PCIE_3A_CFG] = &qhs_pcie3a_cfg,
	[SLAVE_PCIE_3B_CFG] = &qhs_pcie3b_cfg,
	[SLAVE_PCIE_4_CFG] = &qhs_pcie4_cfg,
	[SLAVE_PCIE_5_CFG] = &qhs_pcie5_cfg,
	[SLAVE_PCIE_6_CFG] = &qhs_pcie6_cfg,
	[SLAVE_PCIE_RSCC] = &qhs_pcie_rscc,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QSPI_0] = &qhs_qspi,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_QUP_2] = &qhs_qup2,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SDCC_4] = &qhs_sdc4,
	[SLAVE_SMMUV3_CFG] = &qhs_smmuv3_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB2] = &qhs_usb2_0_cfg,
	[SLAVE_USB3_0] = &qhs_usb3_0_cfg,
	[SLAVE_USB3_1] = &qhs_usb3_1_cfg,
	[SLAVE_USB3_2] = &qhs_usb3_2_cfg,
	[SLAVE_USB3_MP] = &qhs_usb3_mp_cfg,
	[SLAVE_USB4_0] = &qhs_usb4_0_cfg,
	[SLAVE_USB4_1] = &qhs_usb4_1_cfg,
	[SLAVE_USB4_2] = &qhs_usb4_2_cfg,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_CNOC_PCIE_SLAVE_EAST_CFG] = &qss_cnoc_pcie_slave_east_cfg,
	[SLAVE_CNOC_PCIE_SLAVE_WEST_CFG] = &qss_cnoc_pcie_slave_west_cfg,
	[SLAVE_LPASS_QTB_CFG] = &qss_lpass_qtb_cfg,
	[SLAVE_CNOC_MNOC_CFG] = &qss_mnoc_cfg,
	[SLAVE_NSP_QTB_CFG] = &qss_nsp_qtb_cfg,
	[SLAVE_PCIE_EAST_ANOC_CFG] = &qss_pcie_east_anoc_cfg,
	[SLAVE_PCIE_WEST_ANOC_CFG] = &qss_pcie_west_anoc_cfg,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct regmap_config glymur_cnoc_cfg_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x6600,
	.fast_io = true,
};

static const struct qcom_icc_desc glymur_cnoc_cfg = {
	.config = &glymur_cnoc_cfg_regmap_config,
	.nodes = cnoc_cfg_nodes,
	.num_nodes = ARRAY_SIZE(cnoc_cfg_nodes),
	.bcms = cnoc_cfg_bcms,
	.num_bcms = ARRAY_SIZE(cnoc_cfg_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const cnoc_main_bcms[] = {
	&bcm_cn0,
};

static struct qcom_icc_node * const cnoc_main_nodes[] = {
	[MASTER_HSCNOC_CNOC] = &qnm_hscnoc_cnoc,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_IPC_ROUTER_CFG] = &qhs_ipc_router,
	[SLAVE_SOCCP] = &qhs_soccp,
	[SLAVE_TME_CFG] = &qhs_tme_cfg,
	[SLAVE_APPSS] = &qns_apss,
	[SLAVE_CNOC_CFG] = &qss_cfg,
	[SLAVE_BOOT_IMEM] = &qxs_boot_imem,
	[SLAVE_IMEM] = &qxs_imem,
};

static const struct regmap_config glymur_cnoc_main_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x17080,
	.fast_io = true,
};

static const struct qcom_icc_desc glymur_cnoc_main = {
	.config = &glymur_cnoc_main_regmap_config,
	.nodes = cnoc_main_nodes,
	.num_nodes = ARRAY_SIZE(cnoc_main_nodes),
	.bcms = cnoc_main_bcms,
	.num_bcms = ARRAY_SIZE(cnoc_main_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const hscnoc_bcms[] = {
	&bcm_sh0,
	&bcm_sh1,
};

static struct qcom_icc_node * const hscnoc_nodes[] = {
	[MASTER_GPU_TCU] = &alm_gpu_tcu,
	[MASTER_PCIE_TCU] = &alm_pcie_qtc,
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_APPSS_PROC] = &chm_apps,
	[MASTER_AGGRE_NOC_EAST] = &qnm_aggre_noc_east,
	[MASTER_GFX3D] = &qnm_gpu,
	[MASTER_LPASS_GEM_NOC] = &qnm_lpass,
	[MASTER_MNOC_HF_MEM_NOC] = &qnm_mnoc_hf,
	[MASTER_MNOC_SF_MEM_NOC] = &qnm_mnoc_sf,
	[MASTER_COMPUTE_NOC] = &qnm_nsp_noc,
	[MASTER_PCIE_EAST] = &qnm_pcie_east,
	[MASTER_PCIE_WEST] = &qnm_pcie_west,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[MASTER_WLAN_Q6] = &qxm_wlan_q6,
	[MASTER_GIC] = &xm_gic,
	[SLAVE_HSCNOC_CNOC] = &qns_hscnoc_cnoc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_PCIE_EAST] = &qns_pcie_east,
	[SLAVE_PCIE_WEST] = &qns_pcie_west,
};

static const struct regmap_config glymur_hscnoc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x93a080,
	.fast_io = true,
};

static const struct qcom_icc_desc glymur_hscnoc = {
	.config = &glymur_hscnoc_regmap_config,
	.nodes = hscnoc_nodes,
	.num_nodes = ARRAY_SIZE(hscnoc_nodes),
	.bcms = hscnoc_bcms,
	.num_bcms = ARRAY_SIZE(hscnoc_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_node * const lpass_ag_noc_nodes[] = {
	[MASTER_LPIAON_NOC] = &qnm_lpiaon_noc,
	[SLAVE_LPASS_GEM_NOC] = &qns_lpass_ag_noc_gemnoc,
};

static const struct regmap_config glymur_lpass_ag_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xe080,
	.fast_io = true,
};

static const struct qcom_icc_desc glymur_lpass_ag_noc = {
	.config = &glymur_lpass_ag_noc_regmap_config,
	.nodes = lpass_ag_noc_nodes,
	.num_nodes = ARRAY_SIZE(lpass_ag_noc_nodes),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const lpass_lpiaon_noc_bcms[] = {
	&bcm_lp0,
};

static struct qcom_icc_node * const lpass_lpiaon_noc_nodes[] = {
	[MASTER_LPASS_LPINOC] = &qnm_lpass_lpinoc,
	[SLAVE_LPIAON_NOC_LPASS_AG_NOC] = &qns_lpass_aggnoc,
};

static const struct regmap_config glymur_lpass_lpiaon_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x19080,
	.fast_io = true,
};

static const struct qcom_icc_desc glymur_lpass_lpiaon_noc = {
	.config = &glymur_lpass_lpiaon_noc_regmap_config,
	.nodes = lpass_lpiaon_noc_nodes,
	.num_nodes = ARRAY_SIZE(lpass_lpiaon_noc_nodes),
	.bcms = lpass_lpiaon_noc_bcms,
	.num_bcms = ARRAY_SIZE(lpass_lpiaon_noc_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_node * const lpass_lpicx_noc_nodes[] = {
	[MASTER_LPASS_PROC] = &qnm_lpinoc_dsp_qns4m,
	[SLAVE_LPICX_NOC_LPIAON_NOC] = &qns_lpi_aon_noc,
};

static const struct regmap_config glymur_lpass_lpicx_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x44080,
	.fast_io = true,
};

static const struct qcom_icc_desc glymur_lpass_lpicx_noc = {
	.config = &glymur_lpass_lpicx_noc_regmap_config,
	.nodes = lpass_lpicx_noc_nodes,
	.num_nodes = ARRAY_SIZE(lpass_lpicx_noc_nodes),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
};

static struct qcom_icc_node * const mc_virt_nodes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
};

static const struct qcom_icc_desc glymur_mc_virt = {
	.nodes = mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const mmss_noc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
};

static struct qcom_icc_node * const mmss_noc_nodes[] = {
	[MASTER_AV1_ENC] = &qnm_av1_enc,
	[MASTER_CAMNOC_HF] = &qnm_camnoc_hf,
	[MASTER_CAMNOC_ICP] = &qnm_camnoc_icp,
	[MASTER_CAMNOC_SF] = &qnm_camnoc_sf,
	[MASTER_EVA] = &qnm_eva,
	[MASTER_MDP] = &qnm_mdp,
	[MASTER_CDSP_HCP] = &qnm_vapss_hcp,
	[MASTER_VIDEO] = &qnm_video,
	[MASTER_VIDEO_CV_PROC] = &qnm_video_cv_cpu,
	[MASTER_VIDEO_V_PROC] = &qnm_video_v_cpu,
	[MASTER_CNOC_MNOC_CFG] = &qsm_mnoc_cfg,
	[SLAVE_MNOC_HF_MEM_NOC] = &qns_mem_noc_hf,
	[SLAVE_MNOC_SF_MEM_NOC] = &qns_mem_noc_sf,
	[SLAVE_SERVICE_MNOC] = &srvc_mnoc,
};

static const struct regmap_config glymur_mmss_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x5b800,
	.fast_io = true,
};

static const struct qcom_icc_desc glymur_mmss_noc = {
	.config = &glymur_mmss_noc_regmap_config,
	.nodes = mmss_noc_nodes,
	.num_nodes = ARRAY_SIZE(mmss_noc_nodes),
	.bcms = mmss_noc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_noc_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_node * const nsinoc_nodes[] = {
	[MASTER_CPUCP] = &xm_cpucp,
	[SLAVE_NSINOC_SYSTEM_NOC] = &qns_system_noc,
	[SLAVE_SERVICE_NSINOC] = &srvc_nsinoc,
};

static const struct regmap_config glymur_nsinoc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x14080,
	.fast_io = true,
};

static const struct qcom_icc_desc glymur_nsinoc = {
	.config = &glymur_nsinoc_regmap_config,
	.nodes = nsinoc_nodes,
	.num_nodes = ARRAY_SIZE(nsinoc_nodes),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const nsp_noc_bcms[] = {
	&bcm_co0,
};

static struct qcom_icc_node * const nsp_noc_nodes[] = {
	[MASTER_CDSP_PROC] = &qnm_nsp,
	[SLAVE_NSP0_HSC_NOC] = &qns_nsp_hscnoc,
};

static const struct regmap_config glymur_nsp_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x21280,
	.fast_io = true,
};

static const struct qcom_icc_desc glymur_nsp_noc = {
	.config = &glymur_nsp_noc_regmap_config,
	.nodes = nsp_noc_nodes,
	.num_nodes = ARRAY_SIZE(nsp_noc_nodes),
	.bcms = nsp_noc_bcms,
	.num_bcms = ARRAY_SIZE(nsp_noc_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_node * const oobm_ss_noc_nodes[] = {
	[MASTER_OOBMSS_SP_PROC] = &xm_mem_sp,
	[SLAVE_OOBMSS_SNOC] = &qns_oobmss_snoc,
};

static const struct regmap_config glymur_oobm_ss_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x1e080,
	.fast_io = true,
};

static const struct qcom_icc_desc glymur_oobm_ss_noc = {
	.config = &glymur_oobm_ss_noc_regmap_config,
	.nodes = oobm_ss_noc_nodes,
	.num_nodes = ARRAY_SIZE(oobm_ss_noc_nodes),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const pcie_east_anoc_bcms[] = {
	&bcm_sn6,
};

static struct qcom_icc_node * const pcie_east_anoc_nodes[] = {
	[MASTER_PCIE_EAST_ANOC_CFG] = &qsm_pcie_east_anoc_cfg,
	[MASTER_PCIE_0] = &xm_pcie_0,
	[MASTER_PCIE_1] = &xm_pcie_1,
	[MASTER_PCIE_5] = &xm_pcie_5,
	[SLAVE_PCIE_EAST_MEM_NOC] = &qns_pcie_east_mem_noc,
	[SLAVE_SERVICE_PCIE_EAST_AGGRE_NOC] = &srvc_pcie_east_aggre_noc,
};

static const struct regmap_config glymur_pcie_east_anoc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xf300,
	.fast_io = true,
};

static const struct qcom_icc_desc glymur_pcie_east_anoc = {
	.config = &glymur_pcie_east_anoc_regmap_config,
	.nodes = pcie_east_anoc_nodes,
	.num_nodes = ARRAY_SIZE(pcie_east_anoc_nodes),
	.bcms = pcie_east_anoc_bcms,
	.num_bcms = ARRAY_SIZE(pcie_east_anoc_bcms),
	.alloc_dyn_id = true,
	.qos_requires_clocks = true,
};

static struct qcom_icc_bcm * const pcie_east_slv_noc_bcms[] = {
	&bcm_sn6,
};

static struct qcom_icc_node * const pcie_east_slv_noc_nodes[] = {
	[MASTER_HSCNOC_PCIE_EAST] = &qnm_hscnoc_pcie_east,
	[MASTER_CNOC_PCIE_EAST_SLAVE_CFG] = &qsm_cnoc_pcie_east_slave_cfg,
	[SLAVE_HSCNOC_PCIE_EAST_MS_MPU_CFG] = &qhs_hscnoc_pcie_east_ms_mpu_cfg,
	[SLAVE_SERVICE_PCIE_EAST] = &srvc_pcie_east,
	[SLAVE_PCIE_0] = &xs_pcie_0,
	[SLAVE_PCIE_1] = &xs_pcie_1,
	[SLAVE_PCIE_5] = &xs_pcie_5,
};

static const struct regmap_config glymur_pcie_east_slv_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xe080,
	.fast_io = true,
};

static const struct qcom_icc_desc glymur_pcie_east_slv_noc = {
	.config = &glymur_pcie_east_slv_noc_regmap_config,
	.nodes = pcie_east_slv_noc_nodes,
	.num_nodes = ARRAY_SIZE(pcie_east_slv_noc_nodes),
	.bcms = pcie_east_slv_noc_bcms,
	.num_bcms = ARRAY_SIZE(pcie_east_slv_noc_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const pcie_west_anoc_bcms[] = {
	&bcm_sn6,
};

static struct qcom_icc_node * const pcie_west_anoc_nodes[] = {
	[MASTER_PCIE_WEST_ANOC_CFG] = &qsm_pcie_west_anoc_cfg,
	[MASTER_PCIE_2] = &xm_pcie_2,
	[MASTER_PCIE_3A] = &xm_pcie_3a,
	[MASTER_PCIE_3B] = &xm_pcie_3b,
	[MASTER_PCIE_4] = &xm_pcie_4,
	[MASTER_PCIE_6] = &xm_pcie_6,
	[SLAVE_PCIE_WEST_MEM_NOC] = &qns_pcie_west_mem_noc,
	[SLAVE_SERVICE_PCIE_WEST_AGGRE_NOC] = &srvc_pcie_west_aggre_noc,
};

static const struct regmap_config glymur_pcie_west_anoc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xf580,
	.fast_io = true,
};

static const struct qcom_icc_desc glymur_pcie_west_anoc = {
	.config = &glymur_pcie_west_anoc_regmap_config,
	.nodes = pcie_west_anoc_nodes,
	.num_nodes = ARRAY_SIZE(pcie_west_anoc_nodes),
	.bcms = pcie_west_anoc_bcms,
	.num_bcms = ARRAY_SIZE(pcie_west_anoc_bcms),
	.alloc_dyn_id = true,
	.qos_requires_clocks = true,
};

static struct qcom_icc_bcm * const pcie_west_slv_noc_bcms[] = {
	&bcm_sn6,
};

static struct qcom_icc_node * const pcie_west_slv_noc_nodes[] = {
	[MASTER_HSCNOC_PCIE_WEST] = &qnm_hscnoc_pcie_west,
	[MASTER_CNOC_PCIE_WEST_SLAVE_CFG] = &qsm_cnoc_pcie_west_slave_cfg,
	[SLAVE_HSCNOC_PCIE_WEST_MS_MPU_CFG] = &qhs_hscnoc_pcie_west_ms_mpu_cfg,
	[SLAVE_SERVICE_PCIE_WEST] = &srvc_pcie_west,
	[SLAVE_PCIE_2] = &xs_pcie_2,
	[SLAVE_PCIE_3A] = &xs_pcie_3a,
	[SLAVE_PCIE_3B] = &xs_pcie_3b,
	[SLAVE_PCIE_4] = &xs_pcie_4,
	[SLAVE_PCIE_6] = &xs_pcie_6,
};

static const struct regmap_config glymur_pcie_west_slv_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xf180,
	.fast_io = true,
};

static const struct qcom_icc_desc glymur_pcie_west_slv_noc = {
	.config = &glymur_pcie_west_slv_noc_regmap_config,
	.nodes = pcie_west_slv_noc_nodes,
	.num_nodes = ARRAY_SIZE(pcie_west_slv_noc_nodes),
	.bcms = pcie_west_slv_noc_bcms,
	.num_bcms = ARRAY_SIZE(pcie_west_slv_noc_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const system_noc_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn2,
	&bcm_sn3,
	&bcm_sn4,
};

static struct qcom_icc_node * const system_noc_nodes[] = {
	[MASTER_A1NOC_SNOC] = &qnm_aggre1_noc,
	[MASTER_A2NOC_SNOC] = &qnm_aggre2_noc,
	[MASTER_A3NOC_SNOC] = &qnm_aggre3_noc,
	[MASTER_NSINOC_SNOC] = &qnm_nsi_noc,
	[MASTER_OOBMSS] = &qnm_oobmss,
	[SLAVE_SNOC_GEM_NOC_SF] = &qns_gemnoc_sf,
};

static const struct regmap_config glymur_system_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x1c080,
	.fast_io = true,
};

static const struct qcom_icc_desc glymur_system_noc = {
	.config = &glymur_system_noc_regmap_config,
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
	.alloc_dyn_id = true,
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,glymur-aggre1-noc", .data = &glymur_aggre1_noc},
	{ .compatible = "qcom,glymur-aggre2-noc", .data = &glymur_aggre2_noc},
	{ .compatible = "qcom,glymur-aggre3-noc", .data = &glymur_aggre3_noc},
	{ .compatible = "qcom,glymur-aggre4-noc", .data = &glymur_aggre4_noc},
	{ .compatible = "qcom,glymur-clk-virt", .data = &glymur_clk_virt},
	{ .compatible = "qcom,glymur-cnoc-cfg", .data = &glymur_cnoc_cfg},
	{ .compatible = "qcom,glymur-cnoc-main", .data = &glymur_cnoc_main},
	{ .compatible = "qcom,glymur-hscnoc", .data = &glymur_hscnoc},
	{ .compatible = "qcom,glymur-lpass-ag-noc", .data = &glymur_lpass_ag_noc},
	{ .compatible = "qcom,glymur-lpass-lpiaon-noc", .data = &glymur_lpass_lpiaon_noc},
	{ .compatible = "qcom,glymur-lpass-lpicx-noc", .data = &glymur_lpass_lpicx_noc},
	{ .compatible = "qcom,glymur-mc-virt", .data = &glymur_mc_virt},
	{ .compatible = "qcom,glymur-mmss-noc", .data = &glymur_mmss_noc},
	{ .compatible = "qcom,glymur-nsinoc", .data = &glymur_nsinoc},
	{ .compatible = "qcom,glymur-nsp-noc", .data = &glymur_nsp_noc},
	{ .compatible = "qcom,glymur-oobm-ss-noc", .data = &glymur_oobm_ss_noc},
	{ .compatible = "qcom,glymur-pcie-east-anoc", .data = &glymur_pcie_east_anoc},
	{ .compatible = "qcom,glymur-pcie-east-slv-noc", .data = &glymur_pcie_east_slv_noc},
	{ .compatible = "qcom,glymur-pcie-west-anoc", .data = &glymur_pcie_west_anoc},
	{ .compatible = "qcom,glymur-pcie-west-slv-noc", .data = &glymur_pcie_west_slv_noc},
	{ .compatible = "qcom,glymur-system-noc", .data = &glymur_system_noc},
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-glymur",
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

MODULE_DESCRIPTION("GLYMUR NoC driver");
MODULE_LICENSE("GPL");
