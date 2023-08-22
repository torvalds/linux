// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm SDX55 interconnect driver
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 *
 * Copyright (c) 2021, Linaro Ltd.
 *
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <dt-bindings/interconnect/qcom,sdx55.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"
#include "sdx55.h"

static struct qcom_icc_node llcc_mc = {
	.name = "llcc_mc",
	.id = SDX55_MASTER_LLCC,
	.channels = 4,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDX55_SLAVE_EBI_CH0 },
};

static struct qcom_icc_node acm_tcu = {
	.name = "acm_tcu",
	.id = SDX55_MASTER_TCU_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 3,
	.links = { SDX55_SLAVE_LLCC,
		   SDX55_SLAVE_MEM_NOC_SNOC,
		   SDX55_SLAVE_MEM_NOC_PCIE_SNOC
	},
};

static struct qcom_icc_node qnm_snoc_gc = {
	.name = "qnm_snoc_gc",
	.id = SDX55_MASTER_SNOC_GC_MEM_NOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX55_SLAVE_LLCC },
};

static struct qcom_icc_node xm_apps_rdwr = {
	.name = "xm_apps_rdwr",
	.id = SDX55_MASTER_AMPSS_M0,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { SDX55_SLAVE_LLCC,
		   SDX55_SLAVE_MEM_NOC_SNOC,
		   SDX55_SLAVE_MEM_NOC_PCIE_SNOC
	},
};

static struct qcom_icc_node qhm_audio = {
	.name = "qhm_audio",
	.id = SDX55_MASTER_AUDIO,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDX55_SLAVE_ANOC_SNOC },
};

static struct qcom_icc_node qhm_blsp1 = {
	.name = "qhm_blsp1",
	.id = SDX55_MASTER_BLSP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDX55_SLAVE_ANOC_SNOC },
};

static struct qcom_icc_node qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = SDX55_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 28,
	.links = { SDX55_SLAVE_SNOC_CFG,
		   SDX55_SLAVE_EMAC_CFG,
		   SDX55_SLAVE_USB3,
		   SDX55_SLAVE_TLMM,
		   SDX55_SLAVE_SPMI_FETCHER,
		   SDX55_SLAVE_QDSS_CFG,
		   SDX55_SLAVE_PDM,
		   SDX55_SLAVE_SNOC_MEM_NOC_GC,
		   SDX55_SLAVE_TCSR,
		   SDX55_SLAVE_CNOC_DDRSS,
		   SDX55_SLAVE_SPMI_VGI_COEX,
		   SDX55_SLAVE_QPIC,
		   SDX55_SLAVE_OCIMEM,
		   SDX55_SLAVE_IPA_CFG,
		   SDX55_SLAVE_USB3_PHY_CFG,
		   SDX55_SLAVE_AOP,
		   SDX55_SLAVE_BLSP_1,
		   SDX55_SLAVE_SDCC_1,
		   SDX55_SLAVE_CNOC_MSS,
		   SDX55_SLAVE_PCIE_PARF,
		   SDX55_SLAVE_ECC_CFG,
		   SDX55_SLAVE_AUDIO,
		   SDX55_SLAVE_AOSS,
		   SDX55_SLAVE_PRNG,
		   SDX55_SLAVE_CRYPTO_0_CFG,
		   SDX55_SLAVE_TCU,
		   SDX55_SLAVE_CLK_CTL,
		   SDX55_SLAVE_IMEM_CFG
	},
};

static struct qcom_icc_node qhm_qpic = {
	.name = "qhm_qpic",
	.id = SDX55_MASTER_QPIC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 5,
	.links = { SDX55_SLAVE_AOSS,
		   SDX55_SLAVE_IPA_CFG,
		   SDX55_SLAVE_ANOC_SNOC,
		   SDX55_SLAVE_AOP,
		   SDX55_SLAVE_AUDIO
	},
};

static struct qcom_icc_node qhm_snoc_cfg = {
	.name = "qhm_snoc_cfg",
	.id = SDX55_MASTER_SNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDX55_SLAVE_SERVICE_SNOC },
};

static struct qcom_icc_node qhm_spmi_fetcher1 = {
	.name = "qhm_spmi_fetcher1",
	.id = SDX55_MASTER_SPMI_FETCHER,
	.channels = 1,
	.buswidth = 4,
	.num_links = 3,
	.links = { SDX55_SLAVE_AOSS,
		   SDX55_SLAVE_ANOC_SNOC,
		   SDX55_SLAVE_AOP
	},
};

static struct qcom_icc_node qnm_aggre_noc = {
	.name = "qnm_aggre_noc",
	.id = SDX55_MASTER_ANOC_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 30,
	.links = { SDX55_SLAVE_PCIE_0,
		   SDX55_SLAVE_SNOC_CFG,
		   SDX55_SLAVE_SDCC_1,
		   SDX55_SLAVE_TLMM,
		   SDX55_SLAVE_SPMI_FETCHER,
		   SDX55_SLAVE_QDSS_CFG,
		   SDX55_SLAVE_PDM,
		   SDX55_SLAVE_SNOC_MEM_NOC_GC,
		   SDX55_SLAVE_TCSR,
		   SDX55_SLAVE_CNOC_DDRSS,
		   SDX55_SLAVE_SPMI_VGI_COEX,
		   SDX55_SLAVE_QDSS_STM,
		   SDX55_SLAVE_QPIC,
		   SDX55_SLAVE_OCIMEM,
		   SDX55_SLAVE_IPA_CFG,
		   SDX55_SLAVE_USB3_PHY_CFG,
		   SDX55_SLAVE_AOP,
		   SDX55_SLAVE_BLSP_1,
		   SDX55_SLAVE_USB3,
		   SDX55_SLAVE_CNOC_MSS,
		   SDX55_SLAVE_PCIE_PARF,
		   SDX55_SLAVE_ECC_CFG,
		   SDX55_SLAVE_APPSS,
		   SDX55_SLAVE_AUDIO,
		   SDX55_SLAVE_AOSS,
		   SDX55_SLAVE_PRNG,
		   SDX55_SLAVE_CRYPTO_0_CFG,
		   SDX55_SLAVE_TCU,
		   SDX55_SLAVE_CLK_CTL,
		   SDX55_SLAVE_IMEM_CFG
	},
};

static struct qcom_icc_node qnm_ipa = {
	.name = "qnm_ipa",
	.id = SDX55_MASTER_IPA,
	.channels = 1,
	.buswidth = 8,
	.num_links = 27,
	.links = { SDX55_SLAVE_SNOC_CFG,
		   SDX55_SLAVE_EMAC_CFG,
		   SDX55_SLAVE_USB3,
		   SDX55_SLAVE_AOSS,
		   SDX55_SLAVE_SPMI_FETCHER,
		   SDX55_SLAVE_QDSS_CFG,
		   SDX55_SLAVE_PDM,
		   SDX55_SLAVE_SNOC_MEM_NOC_GC,
		   SDX55_SLAVE_TCSR,
		   SDX55_SLAVE_CNOC_DDRSS,
		   SDX55_SLAVE_QDSS_STM,
		   SDX55_SLAVE_QPIC,
		   SDX55_SLAVE_OCIMEM,
		   SDX55_SLAVE_IPA_CFG,
		   SDX55_SLAVE_USB3_PHY_CFG,
		   SDX55_SLAVE_AOP,
		   SDX55_SLAVE_BLSP_1,
		   SDX55_SLAVE_SDCC_1,
		   SDX55_SLAVE_CNOC_MSS,
		   SDX55_SLAVE_PCIE_PARF,
		   SDX55_SLAVE_ECC_CFG,
		   SDX55_SLAVE_AUDIO,
		   SDX55_SLAVE_TLMM,
		   SDX55_SLAVE_PRNG,
		   SDX55_SLAVE_CRYPTO_0_CFG,
		   SDX55_SLAVE_CLK_CTL,
		   SDX55_SLAVE_IMEM_CFG
	},
};

static struct qcom_icc_node qnm_memnoc = {
	.name = "qnm_memnoc",
	.id = SDX55_MASTER_MEM_NOC_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 29,
	.links = { SDX55_SLAVE_SNOC_CFG,
		   SDX55_SLAVE_EMAC_CFG,
		   SDX55_SLAVE_USB3,
		   SDX55_SLAVE_TLMM,
		   SDX55_SLAVE_SPMI_FETCHER,
		   SDX55_SLAVE_QDSS_CFG,
		   SDX55_SLAVE_PDM,
		   SDX55_SLAVE_TCSR,
		   SDX55_SLAVE_CNOC_DDRSS,
		   SDX55_SLAVE_SPMI_VGI_COEX,
		   SDX55_SLAVE_QDSS_STM,
		   SDX55_SLAVE_QPIC,
		   SDX55_SLAVE_OCIMEM,
		   SDX55_SLAVE_IPA_CFG,
		   SDX55_SLAVE_USB3_PHY_CFG,
		   SDX55_SLAVE_AOP,
		   SDX55_SLAVE_BLSP_1,
		   SDX55_SLAVE_SDCC_1,
		   SDX55_SLAVE_CNOC_MSS,
		   SDX55_SLAVE_PCIE_PARF,
		   SDX55_SLAVE_ECC_CFG,
		   SDX55_SLAVE_APPSS,
		   SDX55_SLAVE_AUDIO,
		   SDX55_SLAVE_AOSS,
		   SDX55_SLAVE_PRNG,
		   SDX55_SLAVE_CRYPTO_0_CFG,
		   SDX55_SLAVE_TCU,
		   SDX55_SLAVE_CLK_CTL,
		   SDX55_SLAVE_IMEM_CFG
	},
};

static struct qcom_icc_node qnm_memnoc_pcie = {
	.name = "qnm_memnoc_pcie",
	.id = SDX55_MASTER_MEM_NOC_PCIE_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX55_SLAVE_PCIE_0 },
};

static struct qcom_icc_node qxm_crypto = {
	.name = "qxm_crypto",
	.id = SDX55_MASTER_CRYPTO_CORE_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 3,
	.links = { SDX55_SLAVE_AOSS,
		   SDX55_SLAVE_ANOC_SNOC,
		   SDX55_SLAVE_AOP
	},
};

static struct qcom_icc_node xm_emac = {
	.name = "xm_emac",
	.id = SDX55_MASTER_EMAC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX55_SLAVE_ANOC_SNOC },
};

static struct qcom_icc_node xm_ipa2pcie_slv = {
	.name = "xm_ipa2pcie_slv",
	.id = SDX55_MASTER_IPA_PCIE,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX55_SLAVE_PCIE_0 },
};

static struct qcom_icc_node xm_pcie = {
	.name = "xm_pcie",
	.id = SDX55_MASTER_PCIE,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX55_SLAVE_ANOC_SNOC },
};

static struct qcom_icc_node xm_qdss_etr = {
	.name = "xm_qdss_etr",
	.id = SDX55_MASTER_QDSS_ETR,
	.channels = 1,
	.buswidth = 8,
	.num_links = 28,
	.links = { SDX55_SLAVE_SNOC_CFG,
		   SDX55_SLAVE_EMAC_CFG,
		   SDX55_SLAVE_USB3,
		   SDX55_SLAVE_AOSS,
		   SDX55_SLAVE_SPMI_FETCHER,
		   SDX55_SLAVE_QDSS_CFG,
		   SDX55_SLAVE_PDM,
		   SDX55_SLAVE_SNOC_MEM_NOC_GC,
		   SDX55_SLAVE_TCSR,
		   SDX55_SLAVE_CNOC_DDRSS,
		   SDX55_SLAVE_SPMI_VGI_COEX,
		   SDX55_SLAVE_QPIC,
		   SDX55_SLAVE_OCIMEM,
		   SDX55_SLAVE_IPA_CFG,
		   SDX55_SLAVE_USB3_PHY_CFG,
		   SDX55_SLAVE_AOP,
		   SDX55_SLAVE_BLSP_1,
		   SDX55_SLAVE_SDCC_1,
		   SDX55_SLAVE_CNOC_MSS,
		   SDX55_SLAVE_PCIE_PARF,
		   SDX55_SLAVE_ECC_CFG,
		   SDX55_SLAVE_AUDIO,
		   SDX55_SLAVE_AOSS,
		   SDX55_SLAVE_PRNG,
		   SDX55_SLAVE_CRYPTO_0_CFG,
		   SDX55_SLAVE_TCU,
		   SDX55_SLAVE_CLK_CTL,
		   SDX55_SLAVE_IMEM_CFG
	},
};

static struct qcom_icc_node xm_sdc1 = {
	.name = "xm_sdc1",
	.id = SDX55_MASTER_SDCC_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 5,
	.links = { SDX55_SLAVE_AOSS,
		   SDX55_SLAVE_IPA_CFG,
		   SDX55_SLAVE_ANOC_SNOC,
		   SDX55_SLAVE_AOP,
		   SDX55_SLAVE_AUDIO
	},
};

static struct qcom_icc_node xm_usb3 = {
	.name = "xm_usb3",
	.id = SDX55_MASTER_USB3,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX55_SLAVE_ANOC_SNOC },
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.id = SDX55_SLAVE_EBI_CH0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_llcc = {
	.name = "qns_llcc",
	.id = SDX55_SLAVE_LLCC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SDX55_SLAVE_EBI_CH0 },
};

static struct qcom_icc_node qns_memnoc_snoc = {
	.name = "qns_memnoc_snoc",
	.id = SDX55_SLAVE_MEM_NOC_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX55_MASTER_MEM_NOC_SNOC },
};

static struct qcom_icc_node qns_sys_pcie = {
	.name = "qns_sys_pcie",
	.id = SDX55_SLAVE_MEM_NOC_PCIE_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX55_MASTER_MEM_NOC_PCIE_SNOC },
};

static struct qcom_icc_node qhs_aop = {
	.name = "qhs_aop",
	.id = SDX55_SLAVE_AOP,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_aoss = {
	.name = "qhs_aoss",
	.id = SDX55_SLAVE_AOSS,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_apss = {
	.name = "qhs_apss",
	.id = SDX55_SLAVE_APPSS,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_audio = {
	.name = "qhs_audio",
	.id = SDX55_SLAVE_AUDIO,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_blsp1 = {
	.name = "qhs_blsp1",
	.id = SDX55_SLAVE_BLSP_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SDX55_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = SDX55_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ddrss_cfg = {
	.name = "qhs_ddrss_cfg",
	.id = SDX55_SLAVE_CNOC_DDRSS,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ecc_cfg = {
	.name = "qhs_ecc_cfg",
	.id = SDX55_SLAVE_ECC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_emac_cfg = {
	.name = "qhs_emac_cfg",
	.id = SDX55_SLAVE_EMAC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SDX55_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ipa = {
	.name = "qhs_ipa",
	.id = SDX55_SLAVE_IPA_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_mss_cfg = {
	.name = "qhs_mss_cfg",
	.id = SDX55_SLAVE_CNOC_MSS,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie_parf = {
	.name = "qhs_pcie_parf",
	.id = SDX55_SLAVE_PCIE_PARF,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pdm = {
	.name = "qhs_pdm",
	.id = SDX55_SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_prng = {
	.name = "qhs_prng",
	.id = SDX55_SLAVE_PRNG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SDX55_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qpic = {
	.name = "qhs_qpic",
	.id = SDX55_SLAVE_QPIC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_sdc1 = {
	.name = "qhs_sdc1",
	.id = SDX55_SLAVE_SDCC_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_snoc_cfg = {
	.name = "qhs_snoc_cfg",
	.id = SDX55_SLAVE_SNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SDX55_MASTER_SNOC_CFG },
};

static struct qcom_icc_node qhs_spmi_fetcher = {
	.name = "qhs_spmi_fetcher",
	.id = SDX55_SLAVE_SPMI_FETCHER,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_spmi_vgi_coex = {
	.name = "qhs_spmi_vgi_coex",
	.id = SDX55_SLAVE_SPMI_VGI_COEX,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SDX55_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_tlmm = {
	.name = "qhs_tlmm",
	.id = SDX55_SLAVE_TLMM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb3 = {
	.name = "qhs_usb3",
	.id = SDX55_SLAVE_USB3,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb3_phy = {
	.name = "qhs_usb3_phy",
	.id = SDX55_SLAVE_USB3_PHY_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_aggre_noc = {
	.name = "qns_aggre_noc",
	.id = SDX55_SLAVE_ANOC_SNOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX55_MASTER_ANOC_SNOC },
};

static struct qcom_icc_node qns_snoc_memnoc = {
	.name = "qns_snoc_memnoc",
	.id = SDX55_SLAVE_SNOC_MEM_NOC_GC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SDX55_MASTER_SNOC_GC_MEM_NOC },
};

static struct qcom_icc_node qxs_imem = {
	.name = "qxs_imem",
	.id = SDX55_SLAVE_OCIMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node srvc_snoc = {
	.name = "srvc_snoc",
	.id = SDX55_SLAVE_SERVICE_SNOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node xs_pcie = {
	.name = "xs_pcie",
	.id = SDX55_SLAVE_PCIE_0,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SDX55_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SDX55_SLAVE_TCU,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_bcm bcm_mc0 = {
	.name = "MC0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_pn0 = {
	.name = "PN0",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qhm_snoc_cfg },
};

static struct qcom_icc_bcm bcm_sh3 = {
	.name = "SH3",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &xm_apps_rdwr },
};

static struct qcom_icc_bcm bcm_sh4 = {
	.name = "SH4",
	.keepalive = false,
	.num_nodes = 2,
	.nodes = { &qns_memnoc_snoc, &qns_sys_pcie },
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

static struct qcom_icc_bcm bcm_sn3 = {
	.name = "SN3",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &xs_qdss_stm },
};

static struct qcom_icc_bcm bcm_pn3 = {
	.name = "PN3",
	.keepalive = false,
	.num_nodes = 2,
	.nodes = { &qhm_blsp1, &qhm_qpic },
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &xs_sys_tcu_cfg },
};

static struct qcom_icc_bcm bcm_pn5 = {
	.name = "PN5",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_sn6 = {
	.name = "SN6",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &xs_pcie },
};

static struct qcom_icc_bcm bcm_sn7 = {
	.name = "SN7",
	.keepalive = false,
	.num_nodes = 5,
	.nodes = { &qnm_aggre_noc, &xm_emac, &xm_emac, &xm_usb3, &qns_aggre_noc },
};

static struct qcom_icc_bcm bcm_sn8 = {
	.name = "SN8",
	.keepalive = false,
	.num_nodes = 2,
	.nodes = { &qhm_qdss_bam, &xm_qdss_etr },
};

static struct qcom_icc_bcm bcm_sn9 = {
	.name = "SN9",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qnm_memnoc },
};

static struct qcom_icc_bcm bcm_sn10 = {
	.name = "SN10",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qnm_memnoc_pcie },
};

static struct qcom_icc_bcm bcm_sn11 = {
	.name = "SN11",
	.keepalive = false,
	.num_nodes = 2,
	.nodes = { &qnm_ipa, &xm_ipa2pcie_slv },
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_mc0,
};

static struct qcom_icc_node * const mc_virt_nodes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI_CH0] = &ebi,
};

static const struct qcom_icc_desc sdx55_mc_virt = {
	.nodes = mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm * const mem_noc_bcms[] = {
	&bcm_sh0,
	&bcm_sh3,
	&bcm_sh4,
};

static struct qcom_icc_node * const mem_noc_nodes[] = {
	[MASTER_TCU_0] = &acm_tcu,
	[MASTER_SNOC_GC_MEM_NOC] = &qnm_snoc_gc,
	[MASTER_AMPSS_M0] = &xm_apps_rdwr,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_MEM_NOC_SNOC] = &qns_memnoc_snoc,
	[SLAVE_MEM_NOC_PCIE_SNOC] = &qns_sys_pcie,
};

static const struct qcom_icc_desc sdx55_mem_noc = {
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
	&bcm_pn5,
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn3,
	&bcm_sn4,
	&bcm_sn6,
	&bcm_sn7,
	&bcm_sn8,
	&bcm_sn9,
	&bcm_sn10,
	&bcm_sn11,
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
	[MASTER_CRYPTO_CORE_0] = &qxm_crypto,
	[MASTER_EMAC] = &xm_emac,
	[MASTER_IPA_PCIE] = &xm_ipa2pcie_slv,
	[MASTER_PCIE] = &xm_pcie,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[MASTER_SDCC_1] = &xm_sdc1,
	[MASTER_USB3] = &xm_usb3,
	[SLAVE_AOP] = &qhs_aop,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_APPSS] = &qhs_apss,
	[SLAVE_AUDIO] = &qhs_audio,
	[SLAVE_BLSP_1] = &qhs_blsp1,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_CNOC_DDRSS] = &qhs_ddrss_cfg,
	[SLAVE_ECC_CFG] = &qhs_ecc_cfg,
	[SLAVE_EMAC_CFG] = &qhs_emac_cfg,
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
	[SLAVE_OCIMEM] = &qxs_imem,
	[SLAVE_SERVICE_SNOC] = &srvc_snoc,
	[SLAVE_PCIE_0] = &xs_pcie,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sdx55_system_noc = {
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,sdx55-mc-virt",
	  .data = &sdx55_mc_virt},
	{ .compatible = "qcom,sdx55-mem-noc",
	  .data = &sdx55_mem_noc},
	{ .compatible = "qcom,sdx55-system-noc",
	  .data = &sdx55_system_noc},
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-sdx55",
		.of_match_table = qnoc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(qnoc_driver);

MODULE_DESCRIPTION("Qualcomm SDX55 NoC driver");
MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_LICENSE("GPL v2");
