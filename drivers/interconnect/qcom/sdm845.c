// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
 *
 */

#include <asm/div64.h>
#include <dt-bindings/interconnect/qcom,sdm845.h>
#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sort.h>

#include <soc/qcom/cmd-db.h>
#include <soc/qcom/rpmh.h>
#include <soc/qcom/tcs.h>

#define to_qcom_provider(_provider) \
	container_of(_provider, struct qcom_icc_provider, provider)

struct qcom_icc_provider {
	struct icc_provider provider;
	struct device *dev;
	struct qcom_icc_bcm **bcms;
	size_t num_bcms;
};

/**
 * struct bcm_db - Auxiliary data pertaining to each Bus Clock Manager (BCM)
 * @unit: divisor used to convert bytes/sec bw value to an RPMh msg
 * @width: multiplier used to convert bytes/sec bw value to an RPMh msg
 * @vcd: virtual clock domain that this bcm belongs to
 * @reserved: reserved field
 */
struct bcm_db {
	__le32 unit;
	__le16 width;
	u8 vcd;
	u8 reserved;
};

#define SDM845_MAX_LINKS	43
#define SDM845_MAX_BCMS		30
#define SDM845_MAX_BCM_PER_NODE	2
#define SDM845_MAX_VCD		10

/*
 * The AMC bucket denotes constraints that are applied to hardware when
 * icc_set_bw() completes, whereas the WAKE and SLEEP constraints are applied
 * when the execution environment transitions between active and low power mode.
 */
#define QCOM_ICC_BUCKET_AMC		0
#define QCOM_ICC_BUCKET_WAKE		1
#define QCOM_ICC_BUCKET_SLEEP		2
#define QCOM_ICC_NUM_BUCKETS		3
#define QCOM_ICC_TAG_AMC		BIT(QCOM_ICC_BUCKET_AMC)
#define QCOM_ICC_TAG_WAKE		BIT(QCOM_ICC_BUCKET_WAKE)
#define QCOM_ICC_TAG_SLEEP		BIT(QCOM_ICC_BUCKET_SLEEP)
#define QCOM_ICC_TAG_ACTIVE_ONLY	(QCOM_ICC_TAG_AMC | QCOM_ICC_TAG_WAKE)
#define QCOM_ICC_TAG_ALWAYS		(QCOM_ICC_TAG_AMC | QCOM_ICC_TAG_WAKE |\
					 QCOM_ICC_TAG_SLEEP)

/**
 * struct qcom_icc_node - Qualcomm specific interconnect nodes
 * @name: the node name used in debugfs
 * @links: an array of nodes where we can go next while traversing
 * @id: a unique node identifier
 * @num_links: the total number of @links
 * @channels: num of channels at this node
 * @buswidth: width of the interconnect between a node and the bus
 * @sum_avg: current sum aggregate value of all avg bw requests
 * @max_peak: current max aggregate value of all peak bw requests
 * @bcms: list of bcms associated with this logical node
 * @num_bcms: num of @bcms
 */
struct qcom_icc_node {
	const char *name;
	u16 links[SDM845_MAX_LINKS];
	u16 id;
	u16 num_links;
	u16 channels;
	u16 buswidth;
	u64 sum_avg[QCOM_ICC_NUM_BUCKETS];
	u64 max_peak[QCOM_ICC_NUM_BUCKETS];
	struct qcom_icc_bcm *bcms[SDM845_MAX_BCM_PER_NODE];
	size_t num_bcms;
};

/**
 * struct qcom_icc_bcm - Qualcomm specific hardware accelerator nodes
 * known as Bus Clock Manager (BCM)
 * @name: the bcm node name used to fetch BCM data from command db
 * @type: latency or bandwidth bcm
 * @addr: address offsets used when voting to RPMH
 * @vote_x: aggregated threshold values, represents sum_bw when @type is bw bcm
 * @vote_y: aggregated threshold values, represents peak_bw when @type is bw bcm
 * @dirty: flag used to indicate whether the bcm needs to be committed
 * @keepalive: flag used to indicate whether a keepalive is required
 * @aux_data: auxiliary data used when calculating threshold values and
 * communicating with RPMh
 * @list: used to link to other bcms when compiling lists for commit
 * @num_nodes: total number of @num_nodes
 * @nodes: list of qcom_icc_nodes that this BCM encapsulates
 */
struct qcom_icc_bcm {
	const char *name;
	u32 type;
	u32 addr;
	u64 vote_x[QCOM_ICC_NUM_BUCKETS];
	u64 vote_y[QCOM_ICC_NUM_BUCKETS];
	bool dirty;
	bool keepalive;
	struct bcm_db aux_data;
	struct list_head list;
	size_t num_nodes;
	struct qcom_icc_node *nodes[];
};

struct qcom_icc_fabric {
	struct qcom_icc_node **nodes;
	size_t num_nodes;
};

struct qcom_icc_desc {
	struct qcom_icc_node **nodes;
	size_t num_nodes;
	struct qcom_icc_bcm **bcms;
	size_t num_bcms;
};

#define DEFINE_QNODE(_name, _id, _channels, _buswidth,			\
			_numlinks, ...)					\
		static struct qcom_icc_node _name = {			\
		.id = _id,						\
		.name = #_name,						\
		.channels = _channels,					\
		.buswidth = _buswidth,					\
		.num_links = _numlinks,					\
		.links = { __VA_ARGS__ },				\
	}

DEFINE_QNODE(qhm_a1noc_cfg, MASTER_A1NOC_CFG, 1, 4, 1, SLAVE_SERVICE_A1NOC);
DEFINE_QNODE(qhm_qup1, MASTER_BLSP_1, 1, 4, 1, SLAVE_A1NOC_SNOC);
DEFINE_QNODE(qhm_tsif, MASTER_TSIF, 1, 4, 1, SLAVE_A1NOC_SNOC);
DEFINE_QNODE(xm_sdc2, MASTER_SDCC_2, 1, 8, 1, SLAVE_A1NOC_SNOC);
DEFINE_QNODE(xm_sdc4, MASTER_SDCC_4, 1, 8, 1, SLAVE_A1NOC_SNOC);
DEFINE_QNODE(xm_ufs_card, MASTER_UFS_CARD, 1, 8, 1, SLAVE_A1NOC_SNOC);
DEFINE_QNODE(xm_ufs_mem, MASTER_UFS_MEM, 1, 8, 1, SLAVE_A1NOC_SNOC);
DEFINE_QNODE(xm_pcie_0, MASTER_PCIE_0, 1, 8, 1, SLAVE_ANOC_PCIE_A1NOC_SNOC);
DEFINE_QNODE(qhm_a2noc_cfg, MASTER_A2NOC_CFG, 1, 4, 1, SLAVE_SERVICE_A2NOC);
DEFINE_QNODE(qhm_qdss_bam, MASTER_QDSS_BAM, 1, 4, 1, SLAVE_A2NOC_SNOC);
DEFINE_QNODE(qhm_qup2, MASTER_BLSP_2, 1, 4, 1, SLAVE_A2NOC_SNOC);
DEFINE_QNODE(qnm_cnoc, MASTER_CNOC_A2NOC, 1, 8, 1, SLAVE_A2NOC_SNOC);
DEFINE_QNODE(qxm_crypto, MASTER_CRYPTO, 1, 8, 1, SLAVE_A2NOC_SNOC);
DEFINE_QNODE(qxm_ipa, MASTER_IPA, 1, 8, 1, SLAVE_A2NOC_SNOC);
DEFINE_QNODE(xm_pcie3_1, MASTER_PCIE_1, 1, 8, 1, SLAVE_ANOC_PCIE_SNOC);
DEFINE_QNODE(xm_qdss_etr, MASTER_QDSS_ETR, 1, 8, 1, SLAVE_A2NOC_SNOC);
DEFINE_QNODE(xm_usb3_0, MASTER_USB3_0, 1, 8, 1, SLAVE_A2NOC_SNOC);
DEFINE_QNODE(xm_usb3_1, MASTER_USB3_1, 1, 8, 1, SLAVE_A2NOC_SNOC);
DEFINE_QNODE(qxm_camnoc_hf0_uncomp, MASTER_CAMNOC_HF0_UNCOMP, 1, 32, 1, SLAVE_CAMNOC_UNCOMP);
DEFINE_QNODE(qxm_camnoc_hf1_uncomp, MASTER_CAMNOC_HF1_UNCOMP, 1, 32, 1, SLAVE_CAMNOC_UNCOMP);
DEFINE_QNODE(qxm_camnoc_sf_uncomp, MASTER_CAMNOC_SF_UNCOMP, 1, 32, 1, SLAVE_CAMNOC_UNCOMP);
DEFINE_QNODE(qhm_spdm, MASTER_SPDM, 1, 4, 1, SLAVE_CNOC_A2NOC);
DEFINE_QNODE(qhm_tic, MASTER_TIC, 1, 4, 43, SLAVE_A1NOC_CFG, SLAVE_A2NOC_CFG, SLAVE_AOP, SLAVE_AOSS, SLAVE_CAMERA_CFG, SLAVE_CLK_CTL, SLAVE_CDSP_CFG, SLAVE_RBCPR_CX_CFG, SLAVE_CRYPTO_0_CFG, SLAVE_DCC_CFG, SLAVE_CNOC_DDRSS, SLAVE_DISPLAY_CFG, SLAVE_GLM, SLAVE_GFX3D_CFG, SLAVE_IMEM_CFG, SLAVE_IPA_CFG, SLAVE_CNOC_MNOC_CFG, SLAVE_PCIE_0_CFG, SLAVE_PCIE_1_CFG, SLAVE_PDM, SLAVE_SOUTH_PHY_CFG, SLAVE_PIMEM_CFG, SLAVE_PRNG, SLAVE_QDSS_CFG, SLAVE_BLSP_2, SLAVE_BLSP_1, SLAVE_SDCC_2, SLAVE_SDCC_4, SLAVE_SNOC_CFG, SLAVE_SPDM_WRAPPER, SLAVE_SPSS_CFG, SLAVE_TCSR, SLAVE_TLMM_NORTH, SLAVE_TLMM_SOUTH, SLAVE_TSIF, SLAVE_UFS_CARD_CFG, SLAVE_UFS_MEM_CFG, SLAVE_USB3_0, SLAVE_USB3_1, SLAVE_VENUS_CFG, SLAVE_VSENSE_CTRL_CFG, SLAVE_CNOC_A2NOC, SLAVE_SERVICE_CNOC);
DEFINE_QNODE(qnm_snoc, MASTER_SNOC_CNOC, 1, 8, 42, SLAVE_A1NOC_CFG, SLAVE_A2NOC_CFG, SLAVE_AOP, SLAVE_AOSS, SLAVE_CAMERA_CFG, SLAVE_CLK_CTL, SLAVE_CDSP_CFG, SLAVE_RBCPR_CX_CFG, SLAVE_CRYPTO_0_CFG, SLAVE_DCC_CFG, SLAVE_CNOC_DDRSS, SLAVE_DISPLAY_CFG, SLAVE_GLM, SLAVE_GFX3D_CFG, SLAVE_IMEM_CFG, SLAVE_IPA_CFG, SLAVE_CNOC_MNOC_CFG, SLAVE_PCIE_0_CFG, SLAVE_PCIE_1_CFG, SLAVE_PDM, SLAVE_SOUTH_PHY_CFG, SLAVE_PIMEM_CFG, SLAVE_PRNG, SLAVE_QDSS_CFG, SLAVE_BLSP_2, SLAVE_BLSP_1, SLAVE_SDCC_2, SLAVE_SDCC_4, SLAVE_SNOC_CFG, SLAVE_SPDM_WRAPPER, SLAVE_SPSS_CFG, SLAVE_TCSR, SLAVE_TLMM_NORTH, SLAVE_TLMM_SOUTH, SLAVE_TSIF, SLAVE_UFS_CARD_CFG, SLAVE_UFS_MEM_CFG, SLAVE_USB3_0, SLAVE_USB3_1, SLAVE_VENUS_CFG, SLAVE_VSENSE_CTRL_CFG, SLAVE_SERVICE_CNOC);
DEFINE_QNODE(xm_qdss_dap, MASTER_QDSS_DAP, 1, 8, 43, SLAVE_A1NOC_CFG, SLAVE_A2NOC_CFG, SLAVE_AOP, SLAVE_AOSS, SLAVE_CAMERA_CFG, SLAVE_CLK_CTL, SLAVE_CDSP_CFG, SLAVE_RBCPR_CX_CFG, SLAVE_CRYPTO_0_CFG, SLAVE_DCC_CFG, SLAVE_CNOC_DDRSS, SLAVE_DISPLAY_CFG, SLAVE_GLM, SLAVE_GFX3D_CFG, SLAVE_IMEM_CFG, SLAVE_IPA_CFG, SLAVE_CNOC_MNOC_CFG, SLAVE_PCIE_0_CFG, SLAVE_PCIE_1_CFG, SLAVE_PDM, SLAVE_SOUTH_PHY_CFG, SLAVE_PIMEM_CFG, SLAVE_PRNG, SLAVE_QDSS_CFG, SLAVE_BLSP_2, SLAVE_BLSP_1, SLAVE_SDCC_2, SLAVE_SDCC_4, SLAVE_SNOC_CFG, SLAVE_SPDM_WRAPPER, SLAVE_SPSS_CFG, SLAVE_TCSR, SLAVE_TLMM_NORTH, SLAVE_TLMM_SOUTH, SLAVE_TSIF, SLAVE_UFS_CARD_CFG, SLAVE_UFS_MEM_CFG, SLAVE_USB3_0, SLAVE_USB3_1, SLAVE_VENUS_CFG, SLAVE_VSENSE_CTRL_CFG, SLAVE_CNOC_A2NOC, SLAVE_SERVICE_CNOC);
DEFINE_QNODE(qhm_cnoc, MASTER_CNOC_DC_NOC, 1, 4, 2, SLAVE_LLCC_CFG, SLAVE_MEM_NOC_CFG);
DEFINE_QNODE(acm_l3, MASTER_APPSS_PROC, 1, 16, 3, SLAVE_GNOC_SNOC, SLAVE_GNOC_MEM_NOC, SLAVE_SERVICE_GNOC);
DEFINE_QNODE(pm_gnoc_cfg, MASTER_GNOC_CFG, 1, 4, 1, SLAVE_SERVICE_GNOC);
DEFINE_QNODE(llcc_mc, MASTER_LLCC, 4, 4, 1, SLAVE_EBI1);
DEFINE_QNODE(acm_tcu, MASTER_TCU_0, 1, 8, 3, SLAVE_MEM_NOC_GNOC, SLAVE_LLCC, SLAVE_MEM_NOC_SNOC);
DEFINE_QNODE(qhm_memnoc_cfg, MASTER_MEM_NOC_CFG, 1, 4, 2, SLAVE_MSS_PROC_MS_MPU_CFG, SLAVE_SERVICE_MEM_NOC);
DEFINE_QNODE(qnm_apps, MASTER_GNOC_MEM_NOC, 2, 32, 1, SLAVE_LLCC);
DEFINE_QNODE(qnm_mnoc_hf, MASTER_MNOC_HF_MEM_NOC, 2, 32, 2, SLAVE_MEM_NOC_GNOC, SLAVE_LLCC);
DEFINE_QNODE(qnm_mnoc_sf, MASTER_MNOC_SF_MEM_NOC, 1, 32, 3, SLAVE_MEM_NOC_GNOC, SLAVE_LLCC, SLAVE_MEM_NOC_SNOC);
DEFINE_QNODE(qnm_snoc_gc, MASTER_SNOC_GC_MEM_NOC, 1, 8, 1, SLAVE_LLCC);
DEFINE_QNODE(qnm_snoc_sf, MASTER_SNOC_SF_MEM_NOC, 1, 16, 2, SLAVE_MEM_NOC_GNOC, SLAVE_LLCC);
DEFINE_QNODE(qxm_gpu, MASTER_GFX3D, 2, 32, 3, SLAVE_MEM_NOC_GNOC, SLAVE_LLCC, SLAVE_MEM_NOC_SNOC);
DEFINE_QNODE(qhm_mnoc_cfg, MASTER_CNOC_MNOC_CFG, 1, 4, 1, SLAVE_SERVICE_MNOC);
DEFINE_QNODE(qxm_camnoc_hf0, MASTER_CAMNOC_HF0, 1, 32, 1, SLAVE_MNOC_HF_MEM_NOC);
DEFINE_QNODE(qxm_camnoc_hf1, MASTER_CAMNOC_HF1, 1, 32, 1, SLAVE_MNOC_HF_MEM_NOC);
DEFINE_QNODE(qxm_camnoc_sf, MASTER_CAMNOC_SF, 1, 32, 1, SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qxm_mdp0, MASTER_MDP0, 1, 32, 1, SLAVE_MNOC_HF_MEM_NOC);
DEFINE_QNODE(qxm_mdp1, MASTER_MDP1, 1, 32, 1, SLAVE_MNOC_HF_MEM_NOC);
DEFINE_QNODE(qxm_rot, MASTER_ROTATOR, 1, 32, 1, SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qxm_venus0, MASTER_VIDEO_P0, 1, 32, 1, SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qxm_venus1, MASTER_VIDEO_P1, 1, 32, 1, SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qxm_venus_arm9, MASTER_VIDEO_PROC, 1, 8, 1, SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qhm_snoc_cfg, MASTER_SNOC_CFG, 1, 4, 1, SLAVE_SERVICE_SNOC);
DEFINE_QNODE(qnm_aggre1_noc, MASTER_A1NOC_SNOC, 1, 16, 6, SLAVE_APPSS, SLAVE_SNOC_CNOC, SLAVE_SNOC_MEM_NOC_SF, SLAVE_IMEM, SLAVE_PIMEM, SLAVE_QDSS_STM);
DEFINE_QNODE(qnm_aggre2_noc, MASTER_A2NOC_SNOC, 1, 16, 9, SLAVE_APPSS, SLAVE_SNOC_CNOC, SLAVE_SNOC_MEM_NOC_SF, SLAVE_IMEM, SLAVE_PCIE_0, SLAVE_PCIE_1, SLAVE_PIMEM, SLAVE_QDSS_STM, SLAVE_TCU);
DEFINE_QNODE(qnm_gladiator_sodv, MASTER_GNOC_SNOC, 1, 8, 8, SLAVE_APPSS, SLAVE_SNOC_CNOC, SLAVE_IMEM, SLAVE_PCIE_0, SLAVE_PCIE_1, SLAVE_PIMEM, SLAVE_QDSS_STM, SLAVE_TCU);
DEFINE_QNODE(qnm_memnoc, MASTER_MEM_NOC_SNOC, 1, 8, 5, SLAVE_APPSS, SLAVE_SNOC_CNOC, SLAVE_IMEM, SLAVE_PIMEM, SLAVE_QDSS_STM);
DEFINE_QNODE(qnm_pcie_anoc, MASTER_ANOC_PCIE_SNOC, 1, 16, 5, SLAVE_APPSS, SLAVE_SNOC_CNOC, SLAVE_SNOC_MEM_NOC_SF, SLAVE_IMEM, SLAVE_QDSS_STM);
DEFINE_QNODE(qxm_pimem, MASTER_PIMEM, 1, 8, 2, SLAVE_SNOC_MEM_NOC_GC, SLAVE_IMEM);
DEFINE_QNODE(xm_gic, MASTER_GIC, 1, 8, 2, SLAVE_SNOC_MEM_NOC_GC, SLAVE_IMEM);
DEFINE_QNODE(qns_a1noc_snoc, SLAVE_A1NOC_SNOC, 1, 16, 1, MASTER_A1NOC_SNOC);
DEFINE_QNODE(srvc_aggre1_noc, SLAVE_SERVICE_A1NOC, 1, 4, 0);
DEFINE_QNODE(qns_pcie_a1noc_snoc, SLAVE_ANOC_PCIE_A1NOC_SNOC, 1, 16, 1, MASTER_ANOC_PCIE_SNOC);
DEFINE_QNODE(qns_a2noc_snoc, SLAVE_A2NOC_SNOC, 1, 16, 1, MASTER_A2NOC_SNOC);
DEFINE_QNODE(qns_pcie_snoc, SLAVE_ANOC_PCIE_SNOC, 1, 16, 1, MASTER_ANOC_PCIE_SNOC);
DEFINE_QNODE(srvc_aggre2_noc, SLAVE_SERVICE_A2NOC, 1, 4, 0);
DEFINE_QNODE(qns_camnoc_uncomp, SLAVE_CAMNOC_UNCOMP, 1, 32, 0);
DEFINE_QNODE(qhs_a1_noc_cfg, SLAVE_A1NOC_CFG, 1, 4, 1, MASTER_A1NOC_CFG);
DEFINE_QNODE(qhs_a2_noc_cfg, SLAVE_A2NOC_CFG, 1, 4, 1, MASTER_A2NOC_CFG);
DEFINE_QNODE(qhs_aop, SLAVE_AOP, 1, 4, 0);
DEFINE_QNODE(qhs_aoss, SLAVE_AOSS, 1, 4, 0);
DEFINE_QNODE(qhs_camera_cfg, SLAVE_CAMERA_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_clk_ctl, SLAVE_CLK_CTL, 1, 4, 0);
DEFINE_QNODE(qhs_compute_dsp_cfg, SLAVE_CDSP_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_cpr_cx, SLAVE_RBCPR_CX_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_crypto0_cfg, SLAVE_CRYPTO_0_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_dcc_cfg, SLAVE_DCC_CFG, 1, 4, 1, MASTER_CNOC_DC_NOC);
DEFINE_QNODE(qhs_ddrss_cfg, SLAVE_CNOC_DDRSS, 1, 4, 0);
DEFINE_QNODE(qhs_display_cfg, SLAVE_DISPLAY_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_glm, SLAVE_GLM, 1, 4, 0);
DEFINE_QNODE(qhs_gpuss_cfg, SLAVE_GFX3D_CFG, 1, 8, 0);
DEFINE_QNODE(qhs_imem_cfg, SLAVE_IMEM_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_ipa, SLAVE_IPA_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_mnoc_cfg, SLAVE_CNOC_MNOC_CFG, 1, 4, 1, MASTER_CNOC_MNOC_CFG);
DEFINE_QNODE(qhs_pcie0_cfg, SLAVE_PCIE_0_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_pcie_gen3_cfg, SLAVE_PCIE_1_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_pdm, SLAVE_PDM, 1, 4, 0);
DEFINE_QNODE(qhs_phy_refgen_south, SLAVE_SOUTH_PHY_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_pimem_cfg, SLAVE_PIMEM_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_prng, SLAVE_PRNG, 1, 4, 0);
DEFINE_QNODE(qhs_qdss_cfg, SLAVE_QDSS_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_qupv3_north, SLAVE_BLSP_2, 1, 4, 0);
DEFINE_QNODE(qhs_qupv3_south, SLAVE_BLSP_1, 1, 4, 0);
DEFINE_QNODE(qhs_sdc2, SLAVE_SDCC_2, 1, 4, 0);
DEFINE_QNODE(qhs_sdc4, SLAVE_SDCC_4, 1, 4, 0);
DEFINE_QNODE(qhs_snoc_cfg, SLAVE_SNOC_CFG, 1, 4, 1, MASTER_SNOC_CFG);
DEFINE_QNODE(qhs_spdm, SLAVE_SPDM_WRAPPER, 1, 4, 0);
DEFINE_QNODE(qhs_spss_cfg, SLAVE_SPSS_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_tcsr, SLAVE_TCSR, 1, 4, 0);
DEFINE_QNODE(qhs_tlmm_north, SLAVE_TLMM_NORTH, 1, 4, 0);
DEFINE_QNODE(qhs_tlmm_south, SLAVE_TLMM_SOUTH, 1, 4, 0);
DEFINE_QNODE(qhs_tsif, SLAVE_TSIF, 1, 4, 0);
DEFINE_QNODE(qhs_ufs_card_cfg, SLAVE_UFS_CARD_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_ufs_mem_cfg, SLAVE_UFS_MEM_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_usb3_0, SLAVE_USB3_0, 1, 4, 0);
DEFINE_QNODE(qhs_usb3_1, SLAVE_USB3_1, 1, 4, 0);
DEFINE_QNODE(qhs_venus_cfg, SLAVE_VENUS_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_vsense_ctrl_cfg, SLAVE_VSENSE_CTRL_CFG, 1, 4, 0);
DEFINE_QNODE(qns_cnoc_a2noc, SLAVE_CNOC_A2NOC, 1, 8, 1, MASTER_CNOC_A2NOC);
DEFINE_QNODE(srvc_cnoc, SLAVE_SERVICE_CNOC, 1, 4, 0);
DEFINE_QNODE(qhs_llcc, SLAVE_LLCC_CFG, 1, 4, 0);
DEFINE_QNODE(qhs_memnoc, SLAVE_MEM_NOC_CFG, 1, 4, 1, MASTER_MEM_NOC_CFG);
DEFINE_QNODE(qns_gladiator_sodv, SLAVE_GNOC_SNOC, 1, 8, 1, MASTER_GNOC_SNOC);
DEFINE_QNODE(qns_gnoc_memnoc, SLAVE_GNOC_MEM_NOC, 2, 32, 1, MASTER_GNOC_MEM_NOC);
DEFINE_QNODE(srvc_gnoc, SLAVE_SERVICE_GNOC, 1, 4, 0);
DEFINE_QNODE(ebi, SLAVE_EBI1, 4, 4, 0);
DEFINE_QNODE(qhs_mdsp_ms_mpu_cfg, SLAVE_MSS_PROC_MS_MPU_CFG, 1, 4, 0);
DEFINE_QNODE(qns_apps_io, SLAVE_MEM_NOC_GNOC, 1, 32, 0);
DEFINE_QNODE(qns_llcc, SLAVE_LLCC, 4, 16, 1, MASTER_LLCC);
DEFINE_QNODE(qns_memnoc_snoc, SLAVE_MEM_NOC_SNOC, 1, 8, 1, MASTER_MEM_NOC_SNOC);
DEFINE_QNODE(srvc_memnoc, SLAVE_SERVICE_MEM_NOC, 1, 4, 0);
DEFINE_QNODE(qns2_mem_noc, SLAVE_MNOC_SF_MEM_NOC, 1, 32, 1, MASTER_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qns_mem_noc_hf, SLAVE_MNOC_HF_MEM_NOC, 2, 32, 1, MASTER_MNOC_HF_MEM_NOC);
DEFINE_QNODE(srvc_mnoc, SLAVE_SERVICE_MNOC, 1, 4, 0);
DEFINE_QNODE(qhs_apss, SLAVE_APPSS, 1, 8, 0);
DEFINE_QNODE(qns_cnoc, SLAVE_SNOC_CNOC, 1, 8, 1, MASTER_SNOC_CNOC);
DEFINE_QNODE(qns_memnoc_gc, SLAVE_SNOC_MEM_NOC_GC, 1, 8, 1, MASTER_SNOC_GC_MEM_NOC);
DEFINE_QNODE(qns_memnoc_sf, SLAVE_SNOC_MEM_NOC_SF, 1, 16, 1, MASTER_SNOC_SF_MEM_NOC);
DEFINE_QNODE(qxs_imem, SLAVE_IMEM, 1, 8, 0);
DEFINE_QNODE(qxs_pcie, SLAVE_PCIE_0, 1, 8, 0);
DEFINE_QNODE(qxs_pcie_gen3, SLAVE_PCIE_1, 1, 8, 0);
DEFINE_QNODE(qxs_pimem, SLAVE_PIMEM, 1, 8, 0);
DEFINE_QNODE(srvc_snoc, SLAVE_SERVICE_SNOC, 1, 4, 0);
DEFINE_QNODE(xs_qdss_stm, SLAVE_QDSS_STM, 1, 4, 0);
DEFINE_QNODE(xs_sys_tcu_cfg, SLAVE_TCU, 1, 8, 0);

#define DEFINE_QBCM(_name, _bcmname, _keepalive, _numnodes, ...)	\
		static struct qcom_icc_bcm _name = {			\
		.name = _bcmname,					\
		.keepalive = _keepalive,				\
		.num_nodes = _numnodes,					\
		.nodes = { __VA_ARGS__ },				\
	}

DEFINE_QBCM(bcm_acv, "ACV", false, 1, &ebi);
DEFINE_QBCM(bcm_mc0, "MC0", true, 1, &ebi);
DEFINE_QBCM(bcm_sh0, "SH0", true, 1, &qns_llcc);
DEFINE_QBCM(bcm_mm0, "MM0", false, 1, &qns_mem_noc_hf);
DEFINE_QBCM(bcm_sh1, "SH1", false, 1, &qns_apps_io);
DEFINE_QBCM(bcm_mm1, "MM1", false, 7, &qxm_camnoc_hf0_uncomp, &qxm_camnoc_hf1_uncomp, &qxm_camnoc_sf_uncomp, &qxm_camnoc_hf0, &qxm_camnoc_hf1, &qxm_mdp0, &qxm_mdp1);
DEFINE_QBCM(bcm_sh2, "SH2", false, 1, &qns_memnoc_snoc);
DEFINE_QBCM(bcm_mm2, "MM2", false, 1, &qns2_mem_noc);
DEFINE_QBCM(bcm_sh3, "SH3", false, 1, &acm_tcu);
DEFINE_QBCM(bcm_mm3, "MM3", false, 5, &qxm_camnoc_sf, &qxm_rot, &qxm_venus0, &qxm_venus1, &qxm_venus_arm9);
DEFINE_QBCM(bcm_sh5, "SH5", false, 1, &qnm_apps);
DEFINE_QBCM(bcm_sn0, "SN0", true, 1, &qns_memnoc_sf);
DEFINE_QBCM(bcm_ce0, "CE0", false, 1, &qxm_crypto);
DEFINE_QBCM(bcm_cn0, "CN0", false, 47, &qhm_spdm, &qhm_tic, &qnm_snoc, &xm_qdss_dap, &qhs_a1_noc_cfg, &qhs_a2_noc_cfg, &qhs_aop, &qhs_aoss, &qhs_camera_cfg, &qhs_clk_ctl, &qhs_compute_dsp_cfg, &qhs_cpr_cx, &qhs_crypto0_cfg, &qhs_dcc_cfg, &qhs_ddrss_cfg, &qhs_display_cfg, &qhs_glm, &qhs_gpuss_cfg, &qhs_imem_cfg, &qhs_ipa, &qhs_mnoc_cfg, &qhs_pcie0_cfg, &qhs_pcie_gen3_cfg, &qhs_pdm, &qhs_phy_refgen_south, &qhs_pimem_cfg, &qhs_prng, &qhs_qdss_cfg, &qhs_qupv3_north, &qhs_qupv3_south, &qhs_sdc2, &qhs_sdc4, &qhs_snoc_cfg, &qhs_spdm, &qhs_spss_cfg, &qhs_tcsr, &qhs_tlmm_north, &qhs_tlmm_south, &qhs_tsif, &qhs_ufs_card_cfg, &qhs_ufs_mem_cfg, &qhs_usb3_0, &qhs_usb3_1, &qhs_venus_cfg, &qhs_vsense_ctrl_cfg, &qns_cnoc_a2noc, &srvc_cnoc);
DEFINE_QBCM(bcm_qup0, "QUP0", false, 2, &qhm_qup1, &qhm_qup2);
DEFINE_QBCM(bcm_sn1, "SN1", false, 1, &qxs_imem);
DEFINE_QBCM(bcm_sn2, "SN2", false, 1, &qns_memnoc_gc);
DEFINE_QBCM(bcm_sn3, "SN3", false, 1, &qns_cnoc);
DEFINE_QBCM(bcm_sn4, "SN4", false, 1, &qxm_pimem);
DEFINE_QBCM(bcm_sn5, "SN5", false, 1, &xs_qdss_stm);
DEFINE_QBCM(bcm_sn6, "SN6", false, 3, &qhs_apss, &srvc_snoc, &xs_sys_tcu_cfg);
DEFINE_QBCM(bcm_sn7, "SN7", false, 1, &qxs_pcie);
DEFINE_QBCM(bcm_sn8, "SN8", false, 1, &qxs_pcie_gen3);
DEFINE_QBCM(bcm_sn9, "SN9", false, 2, &srvc_aggre1_noc, &qnm_aggre1_noc);
DEFINE_QBCM(bcm_sn11, "SN11", false, 2, &srvc_aggre2_noc, &qnm_aggre2_noc);
DEFINE_QBCM(bcm_sn12, "SN12", false, 2, &qnm_gladiator_sodv, &xm_gic);
DEFINE_QBCM(bcm_sn14, "SN14", false, 1, &qnm_pcie_anoc);
DEFINE_QBCM(bcm_sn15, "SN15", false, 1, &qnm_memnoc);

static struct qcom_icc_node *rsc_hlos_nodes[] = {
	[MASTER_APPSS_PROC] = &acm_l3,
	[MASTER_TCU_0] = &acm_tcu,
	[MASTER_LLCC] = &llcc_mc,
	[MASTER_GNOC_CFG] = &pm_gnoc_cfg,
	[MASTER_A1NOC_CFG] = &qhm_a1noc_cfg,
	[MASTER_A2NOC_CFG] = &qhm_a2noc_cfg,
	[MASTER_CNOC_DC_NOC] = &qhm_cnoc,
	[MASTER_MEM_NOC_CFG] = &qhm_memnoc_cfg,
	[MASTER_CNOC_MNOC_CFG] = &qhm_mnoc_cfg,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_BLSP_1] = &qhm_qup1,
	[MASTER_BLSP_2] = &qhm_qup2,
	[MASTER_SNOC_CFG] = &qhm_snoc_cfg,
	[MASTER_SPDM] = &qhm_spdm,
	[MASTER_TIC] = &qhm_tic,
	[MASTER_TSIF] = &qhm_tsif,
	[MASTER_A1NOC_SNOC] = &qnm_aggre1_noc,
	[MASTER_A2NOC_SNOC] = &qnm_aggre2_noc,
	[MASTER_GNOC_MEM_NOC] = &qnm_apps,
	[MASTER_CNOC_A2NOC] = &qnm_cnoc,
	[MASTER_GNOC_SNOC] = &qnm_gladiator_sodv,
	[MASTER_MEM_NOC_SNOC] = &qnm_memnoc,
	[MASTER_MNOC_HF_MEM_NOC] = &qnm_mnoc_hf,
	[MASTER_MNOC_SF_MEM_NOC] = &qnm_mnoc_sf,
	[MASTER_ANOC_PCIE_SNOC] = &qnm_pcie_anoc,
	[MASTER_SNOC_CNOC] = &qnm_snoc,
	[MASTER_SNOC_GC_MEM_NOC] = &qnm_snoc_gc,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[MASTER_CAMNOC_HF0] = &qxm_camnoc_hf0,
	[MASTER_CAMNOC_HF0_UNCOMP] = &qxm_camnoc_hf0_uncomp,
	[MASTER_CAMNOC_HF1] = &qxm_camnoc_hf1,
	[MASTER_CAMNOC_HF1_UNCOMP] = &qxm_camnoc_hf1_uncomp,
	[MASTER_CAMNOC_SF] = &qxm_camnoc_sf,
	[MASTER_CAMNOC_SF_UNCOMP] = &qxm_camnoc_sf_uncomp,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_GFX3D] = &qxm_gpu,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_MDP0] = &qxm_mdp0,
	[MASTER_MDP1] = &qxm_mdp1,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_ROTATOR] = &qxm_rot,
	[MASTER_VIDEO_P0] = &qxm_venus0,
	[MASTER_VIDEO_P1] = &qxm_venus1,
	[MASTER_VIDEO_PROC] = &qxm_venus_arm9,
	[MASTER_GIC] = &xm_gic,
	[MASTER_PCIE_1] = &xm_pcie3_1,
	[MASTER_PCIE_0] = &xm_pcie_0,
	[MASTER_QDSS_DAP] = &xm_qdss_dap,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_SDCC_4] = &xm_sdc4,
	[MASTER_UFS_CARD] = &xm_ufs_card,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_USB3_0] = &xm_usb3_0,
	[MASTER_USB3_1] = &xm_usb3_1,
	[SLAVE_EBI1] = &ebi,
	[SLAVE_A1NOC_CFG] = &qhs_a1_noc_cfg,
	[SLAVE_A2NOC_CFG] = &qhs_a2_noc_cfg,
	[SLAVE_AOP] = &qhs_aop,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_APPSS] = &qhs_apss,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CDSP_CFG] = &qhs_compute_dsp_cfg,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_DCC_CFG] = &qhs_dcc_cfg,
	[SLAVE_CNOC_DDRSS] = &qhs_ddrss_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_GLM] = &qhs_glm,
	[SLAVE_GFX3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_LLCC_CFG] = &qhs_llcc,
	[SLAVE_MSS_PROC_MS_MPU_CFG] = &qhs_mdsp_ms_mpu_cfg,
	[SLAVE_MEM_NOC_CFG] = &qhs_memnoc,
	[SLAVE_CNOC_MNOC_CFG] = &qhs_mnoc_cfg,
	[SLAVE_PCIE_0_CFG] = &qhs_pcie0_cfg,
	[SLAVE_PCIE_1_CFG] = &qhs_pcie_gen3_cfg,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_SOUTH_PHY_CFG] = &qhs_phy_refgen_south,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_BLSP_2] = &qhs_qupv3_north,
	[SLAVE_BLSP_1] = &qhs_qupv3_south,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SDCC_4] = &qhs_sdc4,
	[SLAVE_SNOC_CFG] = &qhs_snoc_cfg,
	[SLAVE_SPDM_WRAPPER] = &qhs_spdm,
	[SLAVE_SPSS_CFG] = &qhs_spss_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM_NORTH] = &qhs_tlmm_north,
	[SLAVE_TLMM_SOUTH] = &qhs_tlmm_south,
	[SLAVE_TSIF] = &qhs_tsif,
	[SLAVE_UFS_CARD_CFG] = &qhs_ufs_card_cfg,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB3_0] = &qhs_usb3_0,
	[SLAVE_USB3_1] = &qhs_usb3_1,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_MNOC_SF_MEM_NOC] = &qns2_mem_noc,
	[SLAVE_A1NOC_SNOC] = &qns_a1noc_snoc,
	[SLAVE_A2NOC_SNOC] = &qns_a2noc_snoc,
	[SLAVE_MEM_NOC_GNOC] = &qns_apps_io,
	[SLAVE_CAMNOC_UNCOMP] = &qns_camnoc_uncomp,
	[SLAVE_SNOC_CNOC] = &qns_cnoc,
	[SLAVE_CNOC_A2NOC] = &qns_cnoc_a2noc,
	[SLAVE_GNOC_SNOC] = &qns_gladiator_sodv,
	[SLAVE_GNOC_MEM_NOC] = &qns_gnoc_memnoc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_MNOC_HF_MEM_NOC] = &qns_mem_noc_hf,
	[SLAVE_SNOC_MEM_NOC_GC] = &qns_memnoc_gc,
	[SLAVE_SNOC_MEM_NOC_SF] = &qns_memnoc_sf,
	[SLAVE_MEM_NOC_SNOC] = &qns_memnoc_snoc,
	[SLAVE_ANOC_PCIE_A1NOC_SNOC] = &qns_pcie_a1noc_snoc,
	[SLAVE_ANOC_PCIE_SNOC] = &qns_pcie_snoc,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_PCIE_0] = &qxs_pcie,
	[SLAVE_PCIE_1] = &qxs_pcie_gen3,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SERVICE_A1NOC] = &srvc_aggre1_noc,
	[SLAVE_SERVICE_A2NOC] = &srvc_aggre2_noc,
	[SLAVE_SERVICE_CNOC] = &srvc_cnoc,
	[SLAVE_SERVICE_GNOC] = &srvc_gnoc,
	[SLAVE_SERVICE_MEM_NOC] = &srvc_memnoc,
	[SLAVE_SERVICE_MNOC] = &srvc_mnoc,
	[SLAVE_SERVICE_SNOC] = &srvc_snoc,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static struct qcom_icc_bcm *rsc_hlos_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
	&bcm_sh0,
	&bcm_mm0,
	&bcm_sh1,
	&bcm_mm1,
	&bcm_sh2,
	&bcm_mm2,
	&bcm_sh3,
	&bcm_mm3,
	&bcm_sh5,
	&bcm_sn0,
	&bcm_ce0,
	&bcm_cn0,
	&bcm_qup0,
	&bcm_sn1,
	&bcm_sn2,
	&bcm_sn3,
	&bcm_sn4,
	&bcm_sn5,
	&bcm_sn6,
	&bcm_sn7,
	&bcm_sn8,
	&bcm_sn9,
	&bcm_sn11,
	&bcm_sn12,
	&bcm_sn14,
	&bcm_sn15,
};

static struct qcom_icc_desc sdm845_rsc_hlos = {
	.nodes = rsc_hlos_nodes,
	.num_nodes = ARRAY_SIZE(rsc_hlos_nodes),
	.bcms = rsc_hlos_bcms,
	.num_bcms = ARRAY_SIZE(rsc_hlos_bcms),
};

static int qcom_icc_bcm_init(struct qcom_icc_bcm *bcm, struct device *dev)
{
	struct qcom_icc_node *qn;
	const struct bcm_db *data;
	size_t data_count;
	int i;

	bcm->addr = cmd_db_read_addr(bcm->name);
	if (!bcm->addr) {
		dev_err(dev, "%s could not find RPMh address\n",
			bcm->name);
		return -EINVAL;
	}

	data = cmd_db_read_aux_data(bcm->name, &data_count);
	if (IS_ERR(data)) {
		dev_err(dev, "%s command db read error (%ld)\n",
			bcm->name, PTR_ERR(data));
		return PTR_ERR(data);
	}
	if (!data_count) {
		dev_err(dev, "%s command db missing or partial aux data\n",
			bcm->name);
		return -EINVAL;
	}

	bcm->aux_data.unit = le32_to_cpu(data->unit);
	bcm->aux_data.width = le16_to_cpu(data->width);
	bcm->aux_data.vcd = data->vcd;
	bcm->aux_data.reserved = data->reserved;

	/*
	 * Link Qnodes to their respective BCMs
	 */
	for (i = 0; i < bcm->num_nodes; i++) {
		qn = bcm->nodes[i];
		qn->bcms[qn->num_bcms] = bcm;
		qn->num_bcms++;
	}

	return 0;
}

inline void tcs_cmd_gen(struct tcs_cmd *cmd, u64 vote_x, u64 vote_y,
			u32 addr, bool commit)
{
	bool valid = true;

	if (!cmd)
		return;

	if (vote_x == 0 && vote_y == 0)
		valid = false;

	if (vote_x > BCM_TCS_CMD_VOTE_MASK)
		vote_x = BCM_TCS_CMD_VOTE_MASK;

	if (vote_y > BCM_TCS_CMD_VOTE_MASK)
		vote_y = BCM_TCS_CMD_VOTE_MASK;

	cmd->addr = addr;
	cmd->data = BCM_TCS_CMD(commit, valid, vote_x, vote_y);

	/*
	 * Set the wait for completion flag on command that need to be completed
	 * before the next command.
	 */
	if (commit)
		cmd->wait = true;
}

static void tcs_list_gen(struct list_head *bcm_list, int bucket,
			 struct tcs_cmd tcs_list[SDM845_MAX_VCD],
			 int n[SDM845_MAX_VCD])
{
	struct qcom_icc_bcm *bcm;
	bool commit;
	size_t idx = 0, batch = 0, cur_vcd_size = 0;

	memset(n, 0, sizeof(int) * SDM845_MAX_VCD);

	list_for_each_entry(bcm, bcm_list, list) {
		commit = false;
		cur_vcd_size++;
		if ((list_is_last(&bcm->list, bcm_list)) ||
		    bcm->aux_data.vcd != list_next_entry(bcm, list)->aux_data.vcd) {
			commit = true;
			cur_vcd_size = 0;
		}
		tcs_cmd_gen(&tcs_list[idx], bcm->vote_x[bucket],
			    bcm->vote_y[bucket], bcm->addr, commit);
		idx++;
		n[batch]++;
		/*
		 * Batch the BCMs in such a way that we do not split them in
		 * multiple payloads when they are under the same VCD. This is
		 * to ensure that every BCM is committed since we only set the
		 * commit bit on the last BCM request of every VCD.
		 */
		if (n[batch] >= MAX_RPMH_PAYLOAD) {
			if (!commit) {
				n[batch] -= cur_vcd_size;
				n[batch + 1] = cur_vcd_size;
			}
			batch++;
		}
	}
}

static void bcm_aggregate(struct qcom_icc_bcm *bcm)
{
	size_t i, bucket;
	u64 agg_avg[QCOM_ICC_NUM_BUCKETS] = {0};
	u64 agg_peak[QCOM_ICC_NUM_BUCKETS] = {0};
	u64 temp;

	for (bucket = 0; bucket < QCOM_ICC_NUM_BUCKETS; bucket++) {
		for (i = 0; i < bcm->num_nodes; i++) {
			temp = bcm->nodes[i]->sum_avg[bucket] * bcm->aux_data.width;
			do_div(temp, bcm->nodes[i]->buswidth * bcm->nodes[i]->channels);
			agg_avg[bucket] = max(agg_avg[bucket], temp);

			temp = bcm->nodes[i]->max_peak[bucket] * bcm->aux_data.width;
			do_div(temp, bcm->nodes[i]->buswidth);
			agg_peak[bucket] = max(agg_peak[bucket], temp);
		}

		temp = agg_avg[bucket] * 1000ULL;
		do_div(temp, bcm->aux_data.unit);
		bcm->vote_x[bucket] = temp;

		temp = agg_peak[bucket] * 1000ULL;
		do_div(temp, bcm->aux_data.unit);
		bcm->vote_y[bucket] = temp;
	}

	if (bcm->keepalive && bcm->vote_x[QCOM_ICC_BUCKET_AMC] == 0 &&
	    bcm->vote_y[QCOM_ICC_BUCKET_AMC] == 0) {
		bcm->vote_x[QCOM_ICC_BUCKET_AMC] = 1;
		bcm->vote_x[QCOM_ICC_BUCKET_WAKE] = 1;
		bcm->vote_y[QCOM_ICC_BUCKET_AMC] = 1;
		bcm->vote_y[QCOM_ICC_BUCKET_WAKE] = 1;
	}

	bcm->dirty = false;
}

static void qcom_icc_pre_aggregate(struct icc_node *node)
{
	size_t i;
	struct qcom_icc_node *qn;

	qn = node->data;

	for (i = 0; i < QCOM_ICC_NUM_BUCKETS; i++) {
		qn->sum_avg[i] = 0;
		qn->max_peak[i] = 0;
	}
}

static int qcom_icc_aggregate(struct icc_node *node, u32 tag, u32 avg_bw,
			      u32 peak_bw, u32 *agg_avg, u32 *agg_peak)
{
	size_t i;
	struct qcom_icc_node *qn;

	qn = node->data;

	if (!tag)
		tag = QCOM_ICC_TAG_ALWAYS;

	for (i = 0; i < QCOM_ICC_NUM_BUCKETS; i++) {
		if (tag & BIT(i)) {
			qn->sum_avg[i] += avg_bw;
			qn->max_peak[i] = max_t(u32, qn->max_peak[i], peak_bw);
		}
	}

	*agg_avg += avg_bw;
	*agg_peak = max_t(u32, *agg_peak, peak_bw);

	for (i = 0; i < qn->num_bcms; i++)
		qn->bcms[i]->dirty = true;

	return 0;
}

static int qcom_icc_set(struct icc_node *src, struct icc_node *dst)
{
	struct qcom_icc_provider *qp;
	struct icc_node *node;
	struct tcs_cmd cmds[SDM845_MAX_BCMS];
	struct list_head commit_list;
	int commit_idx[SDM845_MAX_VCD];
	int ret = 0, i;

	if (!src)
		node = dst;
	else
		node = src;

	qp = to_qcom_provider(node->provider);

	INIT_LIST_HEAD(&commit_list);

	for (i = 0; i < qp->num_bcms; i++) {
		if (qp->bcms[i]->dirty) {
			bcm_aggregate(qp->bcms[i]);
			list_add_tail(&qp->bcms[i]->list, &commit_list);
		}
	}

	/*
	 * Construct the command list based on a pre ordered list of BCMs
	 * based on VCD.
	 */
	tcs_list_gen(&commit_list, QCOM_ICC_BUCKET_AMC, cmds, commit_idx);

	if (!commit_idx[0])
		return ret;

	ret = rpmh_invalidate(qp->dev);
	if (ret) {
		pr_err("Error invalidating RPMH client (%d)\n", ret);
		return ret;
	}

	ret = rpmh_write_batch(qp->dev, RPMH_ACTIVE_ONLY_STATE,
			       cmds, commit_idx);
	if (ret) {
		pr_err("Error sending AMC RPMH requests (%d)\n", ret);
		return ret;
	}

	INIT_LIST_HEAD(&commit_list);

	for (i = 0; i < qp->num_bcms; i++) {
		/*
		 * Only generate WAKE and SLEEP commands if a resource's
		 * requirements change as the execution environment transitions
		 * between different power states.
		 */
		if (qp->bcms[i]->vote_x[QCOM_ICC_BUCKET_WAKE] !=
		    qp->bcms[i]->vote_x[QCOM_ICC_BUCKET_SLEEP] ||
		    qp->bcms[i]->vote_y[QCOM_ICC_BUCKET_WAKE] !=
		    qp->bcms[i]->vote_y[QCOM_ICC_BUCKET_SLEEP]) {
			list_add_tail(&qp->bcms[i]->list, &commit_list);
		}
	}

	if (list_empty(&commit_list))
		return ret;

	tcs_list_gen(&commit_list, QCOM_ICC_BUCKET_WAKE, cmds, commit_idx);

	ret = rpmh_write_batch(qp->dev, RPMH_WAKE_ONLY_STATE, cmds, commit_idx);
	if (ret) {
		pr_err("Error sending WAKE RPMH requests (%d)\n", ret);
		return ret;
	}

	tcs_list_gen(&commit_list, QCOM_ICC_BUCKET_SLEEP, cmds, commit_idx);

	ret = rpmh_write_batch(qp->dev, RPMH_SLEEP_STATE, cmds, commit_idx);
	if (ret) {
		pr_err("Error sending SLEEP RPMH requests (%d)\n", ret);
		return ret;
	}

	return ret;
}

static int cmp_vcd(const void *_l, const void *_r)
{
	const struct qcom_icc_bcm **l = (const struct qcom_icc_bcm **)_l;
	const struct qcom_icc_bcm **r = (const struct qcom_icc_bcm **)_r;

	if (l[0]->aux_data.vcd < r[0]->aux_data.vcd)
		return -1;
	else if (l[0]->aux_data.vcd == r[0]->aux_data.vcd)
		return 0;
	else
		return 1;
}

static int qnoc_probe(struct platform_device *pdev)
{
	const struct qcom_icc_desc *desc;
	struct icc_onecell_data *data;
	struct icc_provider *provider;
	struct qcom_icc_node **qnodes;
	struct qcom_icc_provider *qp;
	struct icc_node *node;
	size_t num_nodes, i;
	int ret;

	desc = of_device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	qnodes = desc->nodes;
	num_nodes = desc->num_nodes;

	qp = devm_kzalloc(&pdev->dev, sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return -ENOMEM;

	data = devm_kzalloc(&pdev->dev, struct_size(data, nodes, num_nodes),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	provider = &qp->provider;
	provider->dev = &pdev->dev;
	provider->set = qcom_icc_set;
	provider->pre_aggregate = qcom_icc_pre_aggregate;
	provider->aggregate = qcom_icc_aggregate;
	provider->xlate = of_icc_xlate_onecell;
	INIT_LIST_HEAD(&provider->nodes);
	provider->data = data;

	qp->dev = &pdev->dev;
	qp->bcms = desc->bcms;
	qp->num_bcms = desc->num_bcms;

	ret = icc_provider_add(provider);
	if (ret) {
		dev_err(&pdev->dev, "error adding interconnect provider\n");
		return ret;
	}

	for (i = 0; i < num_nodes; i++) {
		size_t j;

		node = icc_node_create(qnodes[i]->id);
		if (IS_ERR(node)) {
			ret = PTR_ERR(node);
			goto err;
		}

		node->name = qnodes[i]->name;
		node->data = qnodes[i];
		icc_node_add(node, provider);

		dev_dbg(&pdev->dev, "registered node %p %s %d\n", node,
			qnodes[i]->name, node->id);

		/* populate links */
		for (j = 0; j < qnodes[i]->num_links; j++)
			icc_link_create(node, qnodes[i]->links[j]);

		data->nodes[i] = node;
	}
	data->num_nodes = num_nodes;

	for (i = 0; i < qp->num_bcms; i++)
		qcom_icc_bcm_init(qp->bcms[i], &pdev->dev);

	/*
	 * Pre sort the BCMs based on VCD for ease of generating a command list
	 * that groups the BCMs with the same VCD together. VCDs are numbered
	 * with lowest being the most expensive time wise, ensuring that
	 * those commands are being sent the earliest in the queue.
	 */
	sort(qp->bcms, qp->num_bcms, sizeof(*qp->bcms), cmp_vcd, NULL);

	platform_set_drvdata(pdev, qp);

	dev_dbg(&pdev->dev, "Registered SDM845 ICC\n");

	return ret;
err:
	list_for_each_entry(node, &provider->nodes, node_list) {
		icc_node_del(node);
		icc_node_destroy(node->id);
	}

	icc_provider_del(provider);
	return ret;
}

static int qnoc_remove(struct platform_device *pdev)
{
	struct qcom_icc_provider *qp = platform_get_drvdata(pdev);
	struct icc_provider *provider = &qp->provider;
	struct icc_node *n, *tmp;

	list_for_each_entry_safe(n, tmp, &provider->nodes, node_list) {
		icc_node_del(n);
		icc_node_destroy(n->id);
	}

	return icc_provider_del(provider);
}

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,sdm845-rsc-hlos", .data = &sdm845_rsc_hlos },
	{ },
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qnoc_probe,
	.remove = qnoc_remove,
	.driver = {
		.name = "qnoc-sdm845",
		.of_match_table = qnoc_of_match,
	},
};
module_platform_driver(qnoc_driver);

MODULE_AUTHOR("David Dai <daidavid1@codeaurora.org>");
MODULE_DESCRIPTION("Qualcomm sdm845 NoC driver");
MODULE_LICENSE("GPL v2");
