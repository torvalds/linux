// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <dt-bindings/interconnect/qcom,sdx65.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"
#include "sdx65.h"

DEFINE_QNODE(llcc_mc, SDX65_MASTER_LLCC, 1, 4, SDX65_SLAVE_EBI1);
DEFINE_QNODE(acm_tcu, SDX65_MASTER_TCU_0, 1, 8, SDX65_SLAVE_LLCC, SDX65_SLAVE_MEM_NOC_SNOC, SDX65_SLAVE_MEM_NOC_PCIE_SNOC);
DEFINE_QNODE(qnm_snoc_gc, SDX65_MASTER_SNOC_GC_MEM_NOC, 1, 16, SDX65_SLAVE_LLCC);
DEFINE_QNODE(xm_apps_rdwr, SDX65_MASTER_APPSS_PROC, 1, 16, SDX65_SLAVE_LLCC, SDX65_SLAVE_MEM_NOC_SNOC, SDX65_SLAVE_MEM_NOC_PCIE_SNOC);
DEFINE_QNODE(qhm_audio, SDX65_MASTER_AUDIO, 1, 4, SDX65_SLAVE_ANOC_SNOC);
DEFINE_QNODE(qhm_blsp1, SDX65_MASTER_BLSP_1, 1, 4, SDX65_SLAVE_ANOC_SNOC);
DEFINE_QNODE(qhm_qdss_bam, SDX65_MASTER_QDSS_BAM, 1, 4, SDX65_SLAVE_AOSS, SDX65_SLAVE_AUDIO, SDX65_SLAVE_BLSP_1, SDX65_SLAVE_CLK_CTL, SDX65_SLAVE_CRYPTO_0_CFG, SDX65_SLAVE_CNOC_DDRSS, SDX65_SLAVE_ECC_CFG, SDX65_SLAVE_IMEM_CFG, SDX65_SLAVE_IPA_CFG, SDX65_SLAVE_CNOC_MSS, SDX65_SLAVE_PCIE_PARF, SDX65_SLAVE_PDM, SDX65_SLAVE_PRNG, SDX65_SLAVE_QDSS_CFG, SDX65_SLAVE_QPIC, SDX65_SLAVE_SDCC_1, SDX65_SLAVE_SNOC_CFG, SDX65_SLAVE_SPMI_FETCHER, SDX65_SLAVE_SPMI_VGI_COEX, SDX65_SLAVE_TCSR, SDX65_SLAVE_TLMM, SDX65_SLAVE_USB3, SDX65_SLAVE_USB3_PHY_CFG, SDX65_SLAVE_SNOC_MEM_NOC_GC, SDX65_SLAVE_IMEM, SDX65_SLAVE_TCU);
DEFINE_QNODE(qhm_qpic, SDX65_MASTER_QPIC, 1, 4, SDX65_SLAVE_AOSS, SDX65_SLAVE_AUDIO, SDX65_SLAVE_IPA_CFG, SDX65_SLAVE_ANOC_SNOC);
DEFINE_QNODE(qhm_snoc_cfg, SDX65_MASTER_SNOC_CFG, 1, 4, SDX65_SLAVE_SERVICE_SNOC);
DEFINE_QNODE(qhm_spmi_fetcher1, SDX65_MASTER_SPMI_FETCHER, 1, 4, SDX65_SLAVE_AOSS, SDX65_SLAVE_ANOC_SNOC);
DEFINE_QNODE(qnm_aggre_noc, SDX65_MASTER_ANOC_SNOC, 1, 8, SDX65_SLAVE_AOSS, SDX65_SLAVE_APPSS, SDX65_SLAVE_AUDIO, SDX65_SLAVE_BLSP_1, SDX65_SLAVE_CLK_CTL, SDX65_SLAVE_CRYPTO_0_CFG, SDX65_SLAVE_CNOC_DDRSS, SDX65_SLAVE_ECC_CFG, SDX65_SLAVE_IMEM_CFG, SDX65_SLAVE_IPA_CFG, SDX65_SLAVE_CNOC_MSS, SDX65_SLAVE_PCIE_PARF, SDX65_SLAVE_PDM, SDX65_SLAVE_PRNG, SDX65_SLAVE_QDSS_CFG, SDX65_SLAVE_QPIC, SDX65_SLAVE_SDCC_1, SDX65_SLAVE_SNOC_CFG, SDX65_SLAVE_SPMI_FETCHER, SDX65_SLAVE_SPMI_VGI_COEX, SDX65_SLAVE_TCSR, SDX65_SLAVE_TLMM, SDX65_SLAVE_USB3, SDX65_SLAVE_USB3_PHY_CFG, SDX65_SLAVE_SNOC_MEM_NOC_GC, SDX65_SLAVE_IMEM, SDX65_SLAVE_PCIE_0, SDX65_SLAVE_QDSS_STM, SDX65_SLAVE_TCU);
DEFINE_QNODE(qnm_ipa, SDX65_MASTER_IPA, 1, 8, SDX65_SLAVE_AOSS, SDX65_SLAVE_AUDIO, SDX65_SLAVE_BLSP_1, SDX65_SLAVE_CLK_CTL, SDX65_SLAVE_CRYPTO_0_CFG, SDX65_SLAVE_CNOC_DDRSS, SDX65_SLAVE_ECC_CFG, SDX65_SLAVE_IMEM_CFG, SDX65_SLAVE_IPA_CFG, SDX65_SLAVE_CNOC_MSS, SDX65_SLAVE_PCIE_PARF, SDX65_SLAVE_PDM, SDX65_SLAVE_PRNG, SDX65_SLAVE_QDSS_CFG, SDX65_SLAVE_QPIC, SDX65_SLAVE_SDCC_1, SDX65_SLAVE_SNOC_CFG, SDX65_SLAVE_SPMI_FETCHER, SDX65_SLAVE_TCSR, SDX65_SLAVE_TLMM, SDX65_SLAVE_USB3, SDX65_SLAVE_USB3_PHY_CFG, SDX65_SLAVE_SNOC_MEM_NOC_GC, SDX65_SLAVE_IMEM, SDX65_SLAVE_PCIE_0, SDX65_SLAVE_QDSS_STM);
DEFINE_QNODE(qnm_memnoc, SDX65_MASTER_MEM_NOC_SNOC, 1, 8, SDX65_SLAVE_AOSS, SDX65_SLAVE_APPSS, SDX65_SLAVE_AUDIO, SDX65_SLAVE_BLSP_1, SDX65_SLAVE_CLK_CTL, SDX65_SLAVE_CRYPTO_0_CFG, SDX65_SLAVE_CNOC_DDRSS, SDX65_SLAVE_ECC_CFG, SDX65_SLAVE_IMEM_CFG, SDX65_SLAVE_IPA_CFG, SDX65_SLAVE_CNOC_MSS, SDX65_SLAVE_PCIE_PARF, SDX65_SLAVE_PDM, SDX65_SLAVE_PRNG, SDX65_SLAVE_QDSS_CFG, SDX65_SLAVE_QPIC, SDX65_SLAVE_SDCC_1, SDX65_SLAVE_SNOC_CFG, SDX65_SLAVE_SPMI_FETCHER, SDX65_SLAVE_SPMI_VGI_COEX, SDX65_SLAVE_TCSR, SDX65_SLAVE_TLMM, SDX65_SLAVE_USB3, SDX65_SLAVE_USB3_PHY_CFG, SDX65_SLAVE_IMEM, SDX65_SLAVE_QDSS_STM, SDX65_SLAVE_TCU);
DEFINE_QNODE(qnm_memnoc_pcie, SDX65_MASTER_MEM_NOC_PCIE_SNOC, 1, 8, SDX65_SLAVE_PCIE_0);
DEFINE_QNODE(qxm_crypto, SDX65_MASTER_CRYPTO, 1, 8, SDX65_SLAVE_AOSS, SDX65_SLAVE_ANOC_SNOC);
DEFINE_QNODE(xm_ipa2pcie_slv, SDX65_MASTER_IPA_PCIE, 1, 8, SDX65_SLAVE_PCIE_0);
DEFINE_QNODE(xm_pcie, SDX65_MASTER_PCIE_0, 1, 8, SDX65_SLAVE_ANOC_SNOC);
DEFINE_QNODE(xm_qdss_etr, SDX65_MASTER_QDSS_ETR, 1, 8, SDX65_SLAVE_AOSS, SDX65_SLAVE_AUDIO, SDX65_SLAVE_BLSP_1, SDX65_SLAVE_CLK_CTL, SDX65_SLAVE_CRYPTO_0_CFG, SDX65_SLAVE_CNOC_DDRSS, SDX65_SLAVE_ECC_CFG, SDX65_SLAVE_IMEM_CFG, SDX65_SLAVE_IPA_CFG, SDX65_SLAVE_CNOC_MSS, SDX65_SLAVE_PCIE_PARF, SDX65_SLAVE_PDM, SDX65_SLAVE_PRNG, SDX65_SLAVE_QDSS_CFG, SDX65_SLAVE_QPIC, SDX65_SLAVE_SDCC_1, SDX65_SLAVE_SNOC_CFG, SDX65_SLAVE_SPMI_FETCHER, SDX65_SLAVE_SPMI_VGI_COEX, SDX65_SLAVE_TCSR, SDX65_SLAVE_TLMM, SDX65_SLAVE_USB3, SDX65_SLAVE_USB3_PHY_CFG, SDX65_SLAVE_SNOC_MEM_NOC_GC, SDX65_SLAVE_IMEM, SDX65_SLAVE_TCU);
DEFINE_QNODE(xm_sdc1, SDX65_MASTER_SDCC_1, 1, 8, SDX65_SLAVE_AOSS, SDX65_SLAVE_AUDIO, SDX65_SLAVE_IPA_CFG, SDX65_SLAVE_ANOC_SNOC);
DEFINE_QNODE(xm_usb3, SDX65_MASTER_USB3, 1, 8, SDX65_SLAVE_ANOC_SNOC);
DEFINE_QNODE(ebi, SDX65_SLAVE_EBI1, 1, 4);
DEFINE_QNODE(qns_llcc, SDX65_SLAVE_LLCC, 1, 16, SDX65_MASTER_LLCC);
DEFINE_QNODE(qns_memnoc_snoc, SDX65_SLAVE_MEM_NOC_SNOC, 1, 8, SDX65_MASTER_MEM_NOC_SNOC);
DEFINE_QNODE(qns_sys_pcie, SDX65_SLAVE_MEM_NOC_PCIE_SNOC, 1, 8, SDX65_MASTER_MEM_NOC_PCIE_SNOC);
DEFINE_QNODE(qhs_aoss, SDX65_SLAVE_AOSS, 1, 4);
DEFINE_QNODE(qhs_apss, SDX65_SLAVE_APPSS, 1, 4);
DEFINE_QNODE(qhs_audio, SDX65_SLAVE_AUDIO, 1, 4);
DEFINE_QNODE(qhs_blsp1, SDX65_SLAVE_BLSP_1, 1, 4);
DEFINE_QNODE(qhs_clk_ctl, SDX65_SLAVE_CLK_CTL, 1, 4);
DEFINE_QNODE(qhs_crypto0_cfg, SDX65_SLAVE_CRYPTO_0_CFG, 1, 4);
DEFINE_QNODE(qhs_ddrss_cfg, SDX65_SLAVE_CNOC_DDRSS, 1, 4);
DEFINE_QNODE(qhs_ecc_cfg, SDX65_SLAVE_ECC_CFG, 1, 4);
DEFINE_QNODE(qhs_imem_cfg, SDX65_SLAVE_IMEM_CFG, 1, 4);
DEFINE_QNODE(qhs_ipa, SDX65_SLAVE_IPA_CFG, 1, 4);
DEFINE_QNODE(qhs_mss_cfg, SDX65_SLAVE_CNOC_MSS, 1, 4);
DEFINE_QNODE(qhs_pcie_parf, SDX65_SLAVE_PCIE_PARF, 1, 4);
DEFINE_QNODE(qhs_pdm, SDX65_SLAVE_PDM, 1, 4);
DEFINE_QNODE(qhs_prng, SDX65_SLAVE_PRNG, 1, 4);
DEFINE_QNODE(qhs_qdss_cfg, SDX65_SLAVE_QDSS_CFG, 1, 4);
DEFINE_QNODE(qhs_qpic, SDX65_SLAVE_QPIC, 1, 4);
DEFINE_QNODE(qhs_sdc1, SDX65_SLAVE_SDCC_1, 1, 4);
DEFINE_QNODE(qhs_snoc_cfg, SDX65_SLAVE_SNOC_CFG, 1, 4, SDX65_MASTER_SNOC_CFG);
DEFINE_QNODE(qhs_spmi_fetcher, SDX65_SLAVE_SPMI_FETCHER, 1, 4);
DEFINE_QNODE(qhs_spmi_vgi_coex, SDX65_SLAVE_SPMI_VGI_COEX, 1, 4);
DEFINE_QNODE(qhs_tcsr, SDX65_SLAVE_TCSR, 1, 4);
DEFINE_QNODE(qhs_tlmm, SDX65_SLAVE_TLMM, 1, 4);
DEFINE_QNODE(qhs_usb3, SDX65_SLAVE_USB3, 1, 4);
DEFINE_QNODE(qhs_usb3_phy, SDX65_SLAVE_USB3_PHY_CFG, 1, 4);
DEFINE_QNODE(qns_aggre_noc, SDX65_SLAVE_ANOC_SNOC, 1, 8, SDX65_MASTER_ANOC_SNOC);
DEFINE_QNODE(qns_snoc_memnoc, SDX65_SLAVE_SNOC_MEM_NOC_GC, 1, 16, SDX65_MASTER_SNOC_GC_MEM_NOC);
DEFINE_QNODE(qxs_imem, SDX65_SLAVE_IMEM, 1, 8);
DEFINE_QNODE(srvc_snoc, SDX65_SLAVE_SERVICE_SNOC, 1, 4);
DEFINE_QNODE(xs_pcie, SDX65_SLAVE_PCIE_0, 1, 8);
DEFINE_QNODE(xs_qdss_stm, SDX65_SLAVE_QDSS_STM, 1, 4);
DEFINE_QNODE(xs_sys_tcu_cfg, SDX65_SLAVE_TCU, 1, 8);

DEFINE_QBCM(bcm_ce0, "CE0", false, &qxm_crypto);
DEFINE_QBCM(bcm_mc0, "MC0", true, &ebi);
DEFINE_QBCM(bcm_pn0, "PN0", true, &qhm_snoc_cfg, &qhs_aoss, &qhs_apss, &qhs_audio, &qhs_blsp1, &qhs_clk_ctl, &qhs_crypto0_cfg, &qhs_ddrss_cfg, &qhs_ecc_cfg, &qhs_imem_cfg, &qhs_ipa, &qhs_mss_cfg, &qhs_pcie_parf, &qhs_pdm, &qhs_prng, &qhs_qdss_cfg, &qhs_qpic, &qhs_sdc1, &qhs_snoc_cfg, &qhs_spmi_fetcher, &qhs_spmi_vgi_coex, &qhs_tcsr, &qhs_tlmm, &qhs_usb3, &qhs_usb3_phy, &srvc_snoc);
DEFINE_QBCM(bcm_pn1, "PN1", false, &xm_sdc1);
DEFINE_QBCM(bcm_pn2, "PN2", false, &qhm_audio, &qhm_spmi_fetcher1);
DEFINE_QBCM(bcm_pn3, "PN3", false, &qhm_blsp1, &qhm_qpic);
DEFINE_QBCM(bcm_pn4, "PN4", false, &qxm_crypto);
DEFINE_QBCM(bcm_sh0, "SH0", true, &qns_llcc);
DEFINE_QBCM(bcm_sh1, "SH1", false, &qns_memnoc_snoc);
DEFINE_QBCM(bcm_sh3, "SH3", false, &xm_apps_rdwr);
DEFINE_QBCM(bcm_sn0, "SN0", true, &qns_snoc_memnoc);
DEFINE_QBCM(bcm_sn1, "SN1", false, &qxs_imem);
DEFINE_QBCM(bcm_sn2, "SN2", false, &xs_qdss_stm);
DEFINE_QBCM(bcm_sn3, "SN3", false, &xs_sys_tcu_cfg);
DEFINE_QBCM(bcm_sn5, "SN5", false, &xs_pcie);
DEFINE_QBCM(bcm_sn6, "SN6", false, &qhm_qdss_bam, &xm_qdss_etr);
DEFINE_QBCM(bcm_sn7, "SN7", false, &qnm_aggre_noc, &xm_pcie, &xm_usb3, &qns_aggre_noc);
DEFINE_QBCM(bcm_sn8, "SN8", false, &qnm_memnoc);
DEFINE_QBCM(bcm_sn9, "SN9", false, &qnm_memnoc_pcie);
DEFINE_QBCM(bcm_sn10, "SN10", false, &qnm_ipa, &xm_ipa2pcie_slv);

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
