// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Brian Masney <masneyb@onstation.org>
 *
 * Based on MSM bus code from downstream MSM kernel sources.
 * Copyright (c) 2012-2013 The Linux Foundation. All rights reserved.
 *
 * Based on qcs404.c
 * Copyright (C) 2019 Linaro Ltd
 *
 * Here's a rough representation that shows the various buses that form the
 * Network On Chip (NOC) for the msm8974:
 *
 *                         Multimedia Subsystem (MMSS)
 *         |----------+-----------------------------------+-----------|
 *                    |                                   |
 *                    |                                   |
 *        Config      |                     Bus Interface | Memory Controller
 *       |------------+-+-----------|        |------------+-+-----------|
 *                      |                                   |
 *                      |                                   |
 *                      |             System                |
 *     |--------------+-+---------------------------------+-+-------------|
 *                    |                                   |
 *                    |                                   |
 *        Peripheral  |                           On Chip | Memory (OCMEM)
 *       |------------+-------------|        |------------+-------------|
 */

#include <dt-bindings/interconnect/qcom,msm8974.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "smd-rpm.h"

enum {
	MSM8974_BIMC_MAS_AMPSS_M0 = 1,
	MSM8974_BIMC_MAS_AMPSS_M1,
	MSM8974_BIMC_MAS_MSS_PROC,
	MSM8974_BIMC_TO_MNOC,
	MSM8974_BIMC_TO_SNOC,
	MSM8974_BIMC_SLV_EBI_CH0,
	MSM8974_BIMC_SLV_AMPSS_L2,
	MSM8974_CNOC_MAS_RPM_INST,
	MSM8974_CNOC_MAS_RPM_DATA,
	MSM8974_CNOC_MAS_RPM_SYS,
	MSM8974_CNOC_MAS_DEHR,
	MSM8974_CNOC_MAS_QDSS_DAP,
	MSM8974_CNOC_MAS_SPDM,
	MSM8974_CNOC_MAS_TIC,
	MSM8974_CNOC_SLV_CLK_CTL,
	MSM8974_CNOC_SLV_CNOC_MSS,
	MSM8974_CNOC_SLV_SECURITY,
	MSM8974_CNOC_SLV_TCSR,
	MSM8974_CNOC_SLV_TLMM,
	MSM8974_CNOC_SLV_CRYPTO_0_CFG,
	MSM8974_CNOC_SLV_CRYPTO_1_CFG,
	MSM8974_CNOC_SLV_IMEM_CFG,
	MSM8974_CNOC_SLV_MESSAGE_RAM,
	MSM8974_CNOC_SLV_BIMC_CFG,
	MSM8974_CNOC_SLV_BOOT_ROM,
	MSM8974_CNOC_SLV_PMIC_ARB,
	MSM8974_CNOC_SLV_SPDM_WRAPPER,
	MSM8974_CNOC_SLV_DEHR_CFG,
	MSM8974_CNOC_SLV_MPM,
	MSM8974_CNOC_SLV_QDSS_CFG,
	MSM8974_CNOC_SLV_RBCPR_CFG,
	MSM8974_CNOC_SLV_RBCPR_QDSS_APU_CFG,
	MSM8974_CNOC_TO_SNOC,
	MSM8974_CNOC_SLV_CNOC_ONOC_CFG,
	MSM8974_CNOC_SLV_CNOC_MNOC_MMSS_CFG,
	MSM8974_CNOC_SLV_CNOC_MNOC_CFG,
	MSM8974_CNOC_SLV_PNOC_CFG,
	MSM8974_CNOC_SLV_SNOC_MPU_CFG,
	MSM8974_CNOC_SLV_SNOC_CFG,
	MSM8974_CNOC_SLV_EBI1_DLL_CFG,
	MSM8974_CNOC_SLV_PHY_APU_CFG,
	MSM8974_CNOC_SLV_EBI1_PHY_CFG,
	MSM8974_CNOC_SLV_RPM,
	MSM8974_CNOC_SLV_SERVICE_CNOC,
	MSM8974_MNOC_MAS_GRAPHICS_3D,
	MSM8974_MNOC_MAS_JPEG,
	MSM8974_MNOC_MAS_MDP_PORT0,
	MSM8974_MNOC_MAS_VIDEO_P0,
	MSM8974_MNOC_MAS_VIDEO_P1,
	MSM8974_MNOC_MAS_VFE,
	MSM8974_MNOC_TO_CNOC,
	MSM8974_MNOC_TO_BIMC,
	MSM8974_MNOC_SLV_CAMERA_CFG,
	MSM8974_MNOC_SLV_DISPLAY_CFG,
	MSM8974_MNOC_SLV_OCMEM_CFG,
	MSM8974_MNOC_SLV_CPR_CFG,
	MSM8974_MNOC_SLV_CPR_XPU_CFG,
	MSM8974_MNOC_SLV_MISC_CFG,
	MSM8974_MNOC_SLV_MISC_XPU_CFG,
	MSM8974_MNOC_SLV_VENUS_CFG,
	MSM8974_MNOC_SLV_GRAPHICS_3D_CFG,
	MSM8974_MNOC_SLV_MMSS_CLK_CFG,
	MSM8974_MNOC_SLV_MMSS_CLK_XPU_CFG,
	MSM8974_MNOC_SLV_MNOC_MPU_CFG,
	MSM8974_MNOC_SLV_ONOC_MPU_CFG,
	MSM8974_MNOC_SLV_SERVICE_MNOC,
	MSM8974_OCMEM_NOC_TO_OCMEM_VNOC,
	MSM8974_OCMEM_MAS_JPEG_OCMEM,
	MSM8974_OCMEM_MAS_MDP_OCMEM,
	MSM8974_OCMEM_MAS_VIDEO_P0_OCMEM,
	MSM8974_OCMEM_MAS_VIDEO_P1_OCMEM,
	MSM8974_OCMEM_MAS_VFE_OCMEM,
	MSM8974_OCMEM_MAS_CNOC_ONOC_CFG,
	MSM8974_OCMEM_SLV_SERVICE_ONOC,
	MSM8974_OCMEM_VNOC_TO_SNOC,
	MSM8974_OCMEM_VNOC_TO_OCMEM_NOC,
	MSM8974_OCMEM_VNOC_MAS_GFX3D,
	MSM8974_OCMEM_SLV_OCMEM,
	MSM8974_PNOC_MAS_PNOC_CFG,
	MSM8974_PNOC_MAS_SDCC_1,
	MSM8974_PNOC_MAS_SDCC_3,
	MSM8974_PNOC_MAS_SDCC_4,
	MSM8974_PNOC_MAS_SDCC_2,
	MSM8974_PNOC_MAS_TSIF,
	MSM8974_PNOC_MAS_BAM_DMA,
	MSM8974_PNOC_MAS_BLSP_2,
	MSM8974_PNOC_MAS_USB_HSIC,
	MSM8974_PNOC_MAS_BLSP_1,
	MSM8974_PNOC_MAS_USB_HS,
	MSM8974_PNOC_TO_SNOC,
	MSM8974_PNOC_SLV_SDCC_1,
	MSM8974_PNOC_SLV_SDCC_3,
	MSM8974_PNOC_SLV_SDCC_2,
	MSM8974_PNOC_SLV_SDCC_4,
	MSM8974_PNOC_SLV_TSIF,
	MSM8974_PNOC_SLV_BAM_DMA,
	MSM8974_PNOC_SLV_BLSP_2,
	MSM8974_PNOC_SLV_USB_HSIC,
	MSM8974_PNOC_SLV_BLSP_1,
	MSM8974_PNOC_SLV_USB_HS,
	MSM8974_PNOC_SLV_PDM,
	MSM8974_PNOC_SLV_PERIPH_APU_CFG,
	MSM8974_PNOC_SLV_PNOC_MPU_CFG,
	MSM8974_PNOC_SLV_PRNG,
	MSM8974_PNOC_SLV_SERVICE_PNOC,
	MSM8974_SNOC_MAS_LPASS_AHB,
	MSM8974_SNOC_MAS_QDSS_BAM,
	MSM8974_SNOC_MAS_SNOC_CFG,
	MSM8974_SNOC_TO_BIMC,
	MSM8974_SNOC_TO_CNOC,
	MSM8974_SNOC_TO_PNOC,
	MSM8974_SNOC_TO_OCMEM_VNOC,
	MSM8974_SNOC_MAS_CRYPTO_CORE0,
	MSM8974_SNOC_MAS_CRYPTO_CORE1,
	MSM8974_SNOC_MAS_LPASS_PROC,
	MSM8974_SNOC_MAS_MSS,
	MSM8974_SNOC_MAS_MSS_NAV,
	MSM8974_SNOC_MAS_OCMEM_DMA,
	MSM8974_SNOC_MAS_WCSS,
	MSM8974_SNOC_MAS_QDSS_ETR,
	MSM8974_SNOC_MAS_USB3,
	MSM8974_SNOC_SLV_AMPSS,
	MSM8974_SNOC_SLV_LPASS,
	MSM8974_SNOC_SLV_USB3,
	MSM8974_SNOC_SLV_WCSS,
	MSM8974_SNOC_SLV_OCIMEM,
	MSM8974_SNOC_SLV_SNOC_OCMEM,
	MSM8974_SNOC_SLV_SERVICE_SNOC,
	MSM8974_SNOC_SLV_QDSS_STM,
};

#define RPM_BUS_MASTER_REQ	0x73616d62
#define RPM_BUS_SLAVE_REQ	0x766c7362

#define to_msm8974_icc_provider(_provider) \
	container_of(_provider, struct msm8974_icc_provider, provider)

static const struct clk_bulk_data msm8974_icc_bus_clocks[] = {
	{ .id = "bus" },
	{ .id = "bus_a" },
};

/**
 * struct msm8974_icc_provider - Qualcomm specific interconnect provider
 * @provider: generic interconnect provider
 * @bus_clks: the clk_bulk_data table of bus clocks
 * @num_clks: the total number of clk_bulk_data entries
 */
struct msm8974_icc_provider {
	struct icc_provider provider;
	struct clk_bulk_data *bus_clks;
	int num_clks;
};

#define MSM8974_ICC_MAX_LINKS	3

/**
 * struct msm8974_icc_node - Qualcomm specific interconnect nodes
 * @name: the node name used in debugfs
 * @id: a unique node identifier
 * @links: an array of nodes where we can go next while traversing
 * @num_links: the total number of @links
 * @buswidth: width of the interconnect between a node and the bus (bytes)
 * @mas_rpm_id:	RPM ID for devices that are bus masters
 * @slv_rpm_id:	RPM ID for devices that are bus slaves
 * @rate: current bus clock rate in Hz
 */
struct msm8974_icc_node {
	unsigned char *name;
	u16 id;
	u16 links[MSM8974_ICC_MAX_LINKS];
	u16 num_links;
	u16 buswidth;
	int mas_rpm_id;
	int slv_rpm_id;
	u64 rate;
};

struct msm8974_icc_desc {
	struct msm8974_icc_node **nodes;
	size_t num_nodes;
};

#define DEFINE_QNODE(_name, _id, _buswidth, _mas_rpm_id, _slv_rpm_id,	\
		     ...)						\
		static struct msm8974_icc_node _name = {		\
		.name = #_name,						\
		.id = _id,						\
		.buswidth = _buswidth,					\
		.mas_rpm_id = _mas_rpm_id,				\
		.slv_rpm_id = _slv_rpm_id,				\
		.num_links = ARRAY_SIZE(((int[]){ __VA_ARGS__ })),	\
		.links = { __VA_ARGS__ },				\
	}

DEFINE_QNODE(mas_ampss_m0, MSM8974_BIMC_MAS_AMPSS_M0, 8, 0, -1);
DEFINE_QNODE(mas_ampss_m1, MSM8974_BIMC_MAS_AMPSS_M1, 8, 0, -1);
DEFINE_QNODE(mas_mss_proc, MSM8974_BIMC_MAS_MSS_PROC, 8, 1, -1);
DEFINE_QNODE(bimc_to_mnoc, MSM8974_BIMC_TO_MNOC, 8, 2, -1, MSM8974_BIMC_SLV_EBI_CH0);
DEFINE_QNODE(bimc_to_snoc, MSM8974_BIMC_TO_SNOC, 8, 3, 2, MSM8974_SNOC_TO_BIMC, MSM8974_BIMC_SLV_EBI_CH0, MSM8974_BIMC_MAS_AMPSS_M0);
DEFINE_QNODE(slv_ebi_ch0, MSM8974_BIMC_SLV_EBI_CH0, 8, -1, 0);
DEFINE_QNODE(slv_ampss_l2, MSM8974_BIMC_SLV_AMPSS_L2, 8, -1, 1);

static struct msm8974_icc_node *msm8974_bimc_nodes[] = {
	[BIMC_MAS_AMPSS_M0] = &mas_ampss_m0,
	[BIMC_MAS_AMPSS_M1] = &mas_ampss_m1,
	[BIMC_MAS_MSS_PROC] = &mas_mss_proc,
	[BIMC_TO_MNOC] = &bimc_to_mnoc,
	[BIMC_TO_SNOC] = &bimc_to_snoc,
	[BIMC_SLV_EBI_CH0] = &slv_ebi_ch0,
	[BIMC_SLV_AMPSS_L2] = &slv_ampss_l2,
};

static struct msm8974_icc_desc msm8974_bimc = {
	.nodes = msm8974_bimc_nodes,
	.num_nodes = ARRAY_SIZE(msm8974_bimc_nodes),
};

DEFINE_QNODE(mas_rpm_inst, MSM8974_CNOC_MAS_RPM_INST, 8, 45, -1);
DEFINE_QNODE(mas_rpm_data, MSM8974_CNOC_MAS_RPM_DATA, 8, 46, -1);
DEFINE_QNODE(mas_rpm_sys, MSM8974_CNOC_MAS_RPM_SYS, 8, 47, -1);
DEFINE_QNODE(mas_dehr, MSM8974_CNOC_MAS_DEHR, 8, 48, -1);
DEFINE_QNODE(mas_qdss_dap, MSM8974_CNOC_MAS_QDSS_DAP, 8, 49, -1);
DEFINE_QNODE(mas_spdm, MSM8974_CNOC_MAS_SPDM, 8, 50, -1);
DEFINE_QNODE(mas_tic, MSM8974_CNOC_MAS_TIC, 8, 51, -1);
DEFINE_QNODE(slv_clk_ctl, MSM8974_CNOC_SLV_CLK_CTL, 8, -1, 47);
DEFINE_QNODE(slv_cnoc_mss, MSM8974_CNOC_SLV_CNOC_MSS, 8, -1, 48);
DEFINE_QNODE(slv_security, MSM8974_CNOC_SLV_SECURITY, 8, -1, 49);
DEFINE_QNODE(slv_tcsr, MSM8974_CNOC_SLV_TCSR, 8, -1, 50);
DEFINE_QNODE(slv_tlmm, MSM8974_CNOC_SLV_TLMM, 8, -1, 51);
DEFINE_QNODE(slv_crypto_0_cfg, MSM8974_CNOC_SLV_CRYPTO_0_CFG, 8, -1, 52);
DEFINE_QNODE(slv_crypto_1_cfg, MSM8974_CNOC_SLV_CRYPTO_1_CFG, 8, -1, 53);
DEFINE_QNODE(slv_imem_cfg, MSM8974_CNOC_SLV_IMEM_CFG, 8, -1, 54);
DEFINE_QNODE(slv_message_ram, MSM8974_CNOC_SLV_MESSAGE_RAM, 8, -1, 55);
DEFINE_QNODE(slv_bimc_cfg, MSM8974_CNOC_SLV_BIMC_CFG, 8, -1, 56);
DEFINE_QNODE(slv_boot_rom, MSM8974_CNOC_SLV_BOOT_ROM, 8, -1, 57);
DEFINE_QNODE(slv_pmic_arb, MSM8974_CNOC_SLV_PMIC_ARB, 8, -1, 59);
DEFINE_QNODE(slv_spdm_wrapper, MSM8974_CNOC_SLV_SPDM_WRAPPER, 8, -1, 60);
DEFINE_QNODE(slv_dehr_cfg, MSM8974_CNOC_SLV_DEHR_CFG, 8, -1, 61);
DEFINE_QNODE(slv_mpm, MSM8974_CNOC_SLV_MPM, 8, -1, 62);
DEFINE_QNODE(slv_qdss_cfg, MSM8974_CNOC_SLV_QDSS_CFG, 8, -1, 63);
DEFINE_QNODE(slv_rbcpr_cfg, MSM8974_CNOC_SLV_RBCPR_CFG, 8, -1, 64);
DEFINE_QNODE(slv_rbcpr_qdss_apu_cfg, MSM8974_CNOC_SLV_RBCPR_QDSS_APU_CFG, 8, -1, 65);
DEFINE_QNODE(cnoc_to_snoc, MSM8974_CNOC_TO_SNOC, 8, 52, 75);
DEFINE_QNODE(slv_cnoc_onoc_cfg, MSM8974_CNOC_SLV_CNOC_ONOC_CFG, 8, -1, 68);
DEFINE_QNODE(slv_cnoc_mnoc_mmss_cfg, MSM8974_CNOC_SLV_CNOC_MNOC_MMSS_CFG, 8, -1, 58);
DEFINE_QNODE(slv_cnoc_mnoc_cfg, MSM8974_CNOC_SLV_CNOC_MNOC_CFG, 8, -1, 66);
DEFINE_QNODE(slv_pnoc_cfg, MSM8974_CNOC_SLV_PNOC_CFG, 8, -1, 69);
DEFINE_QNODE(slv_snoc_mpu_cfg, MSM8974_CNOC_SLV_SNOC_MPU_CFG, 8, -1, 67);
DEFINE_QNODE(slv_snoc_cfg, MSM8974_CNOC_SLV_SNOC_CFG, 8, -1, 70);
DEFINE_QNODE(slv_ebi1_dll_cfg, MSM8974_CNOC_SLV_EBI1_DLL_CFG, 8, -1, 71);
DEFINE_QNODE(slv_phy_apu_cfg, MSM8974_CNOC_SLV_PHY_APU_CFG, 8, -1, 72);
DEFINE_QNODE(slv_ebi1_phy_cfg, MSM8974_CNOC_SLV_EBI1_PHY_CFG, 8, -1, 73);
DEFINE_QNODE(slv_rpm, MSM8974_CNOC_SLV_RPM, 8, -1, 74);
DEFINE_QNODE(slv_service_cnoc, MSM8974_CNOC_SLV_SERVICE_CNOC, 8, -1, 76);

static struct msm8974_icc_node *msm8974_cnoc_nodes[] = {
	[CNOC_MAS_RPM_INST] = &mas_rpm_inst,
	[CNOC_MAS_RPM_DATA] = &mas_rpm_data,
	[CNOC_MAS_RPM_SYS] = &mas_rpm_sys,
	[CNOC_MAS_DEHR] = &mas_dehr,
	[CNOC_MAS_QDSS_DAP] = &mas_qdss_dap,
	[CNOC_MAS_SPDM] = &mas_spdm,
	[CNOC_MAS_TIC] = &mas_tic,
	[CNOC_SLV_CLK_CTL] = &slv_clk_ctl,
	[CNOC_SLV_CNOC_MSS] = &slv_cnoc_mss,
	[CNOC_SLV_SECURITY] = &slv_security,
	[CNOC_SLV_TCSR] = &slv_tcsr,
	[CNOC_SLV_TLMM] = &slv_tlmm,
	[CNOC_SLV_CRYPTO_0_CFG] = &slv_crypto_0_cfg,
	[CNOC_SLV_CRYPTO_1_CFG] = &slv_crypto_1_cfg,
	[CNOC_SLV_IMEM_CFG] = &slv_imem_cfg,
	[CNOC_SLV_MESSAGE_RAM] = &slv_message_ram,
	[CNOC_SLV_BIMC_CFG] = &slv_bimc_cfg,
	[CNOC_SLV_BOOT_ROM] = &slv_boot_rom,
	[CNOC_SLV_PMIC_ARB] = &slv_pmic_arb,
	[CNOC_SLV_SPDM_WRAPPER] = &slv_spdm_wrapper,
	[CNOC_SLV_DEHR_CFG] = &slv_dehr_cfg,
	[CNOC_SLV_MPM] = &slv_mpm,
	[CNOC_SLV_QDSS_CFG] = &slv_qdss_cfg,
	[CNOC_SLV_RBCPR_CFG] = &slv_rbcpr_cfg,
	[CNOC_SLV_RBCPR_QDSS_APU_CFG] = &slv_rbcpr_qdss_apu_cfg,
	[CNOC_TO_SNOC] = &cnoc_to_snoc,
	[CNOC_SLV_CNOC_ONOC_CFG] = &slv_cnoc_onoc_cfg,
	[CNOC_SLV_CNOC_MNOC_MMSS_CFG] = &slv_cnoc_mnoc_mmss_cfg,
	[CNOC_SLV_CNOC_MNOC_CFG] = &slv_cnoc_mnoc_cfg,
	[CNOC_SLV_PNOC_CFG] = &slv_pnoc_cfg,
	[CNOC_SLV_SNOC_MPU_CFG] = &slv_snoc_mpu_cfg,
	[CNOC_SLV_SNOC_CFG] = &slv_snoc_cfg,
	[CNOC_SLV_EBI1_DLL_CFG] = &slv_ebi1_dll_cfg,
	[CNOC_SLV_PHY_APU_CFG] = &slv_phy_apu_cfg,
	[CNOC_SLV_EBI1_PHY_CFG] = &slv_ebi1_phy_cfg,
	[CNOC_SLV_RPM] = &slv_rpm,
	[CNOC_SLV_SERVICE_CNOC] = &slv_service_cnoc,
};

static struct msm8974_icc_desc msm8974_cnoc = {
	.nodes = msm8974_cnoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8974_cnoc_nodes),
};

DEFINE_QNODE(mas_graphics_3d, MSM8974_MNOC_MAS_GRAPHICS_3D, 16, 6, -1, MSM8974_MNOC_TO_BIMC);
DEFINE_QNODE(mas_jpeg, MSM8974_MNOC_MAS_JPEG, 16, 7, -1, MSM8974_MNOC_TO_BIMC);
DEFINE_QNODE(mas_mdp_port0, MSM8974_MNOC_MAS_MDP_PORT0, 16, 8, -1, MSM8974_MNOC_TO_BIMC);
DEFINE_QNODE(mas_video_p0, MSM8974_MNOC_MAS_VIDEO_P0, 16, 9, -1);
DEFINE_QNODE(mas_video_p1, MSM8974_MNOC_MAS_VIDEO_P1, 16, 10, -1);
DEFINE_QNODE(mas_vfe, MSM8974_MNOC_MAS_VFE, 16, 11, -1, MSM8974_MNOC_TO_BIMC);
DEFINE_QNODE(mnoc_to_cnoc, MSM8974_MNOC_TO_CNOC, 16, 4, -1);
DEFINE_QNODE(mnoc_to_bimc, MSM8974_MNOC_TO_BIMC, 16, -1, 16, MSM8974_BIMC_TO_MNOC);
DEFINE_QNODE(slv_camera_cfg, MSM8974_MNOC_SLV_CAMERA_CFG, 16, -1, 3);
DEFINE_QNODE(slv_display_cfg, MSM8974_MNOC_SLV_DISPLAY_CFG, 16, -1, 4);
DEFINE_QNODE(slv_ocmem_cfg, MSM8974_MNOC_SLV_OCMEM_CFG, 16, -1, 5);
DEFINE_QNODE(slv_cpr_cfg, MSM8974_MNOC_SLV_CPR_CFG, 16, -1, 6);
DEFINE_QNODE(slv_cpr_xpu_cfg, MSM8974_MNOC_SLV_CPR_XPU_CFG, 16, -1, 7);
DEFINE_QNODE(slv_misc_cfg, MSM8974_MNOC_SLV_MISC_CFG, 16, -1, 8);
DEFINE_QNODE(slv_misc_xpu_cfg, MSM8974_MNOC_SLV_MISC_XPU_CFG, 16, -1, 9);
DEFINE_QNODE(slv_venus_cfg, MSM8974_MNOC_SLV_VENUS_CFG, 16, -1, 10);
DEFINE_QNODE(slv_graphics_3d_cfg, MSM8974_MNOC_SLV_GRAPHICS_3D_CFG, 16, -1, 11);
DEFINE_QNODE(slv_mmss_clk_cfg, MSM8974_MNOC_SLV_MMSS_CLK_CFG, 16, -1, 12);
DEFINE_QNODE(slv_mmss_clk_xpu_cfg, MSM8974_MNOC_SLV_MMSS_CLK_XPU_CFG, 16, -1, 13);
DEFINE_QNODE(slv_mnoc_mpu_cfg, MSM8974_MNOC_SLV_MNOC_MPU_CFG, 16, -1, 14);
DEFINE_QNODE(slv_onoc_mpu_cfg, MSM8974_MNOC_SLV_ONOC_MPU_CFG, 16, -1, 15);
DEFINE_QNODE(slv_service_mnoc, MSM8974_MNOC_SLV_SERVICE_MNOC, 16, -1, 17);

static struct msm8974_icc_node *msm8974_mnoc_nodes[] = {
	[MNOC_MAS_GRAPHICS_3D] = &mas_graphics_3d,
	[MNOC_MAS_JPEG] = &mas_jpeg,
	[MNOC_MAS_MDP_PORT0] = &mas_mdp_port0,
	[MNOC_MAS_VIDEO_P0] = &mas_video_p0,
	[MNOC_MAS_VIDEO_P1] = &mas_video_p1,
	[MNOC_MAS_VFE] = &mas_vfe,
	[MNOC_TO_CNOC] = &mnoc_to_cnoc,
	[MNOC_TO_BIMC] = &mnoc_to_bimc,
	[MNOC_SLV_CAMERA_CFG] = &slv_camera_cfg,
	[MNOC_SLV_DISPLAY_CFG] = &slv_display_cfg,
	[MNOC_SLV_OCMEM_CFG] = &slv_ocmem_cfg,
	[MNOC_SLV_CPR_CFG] = &slv_cpr_cfg,
	[MNOC_SLV_CPR_XPU_CFG] = &slv_cpr_xpu_cfg,
	[MNOC_SLV_MISC_CFG] = &slv_misc_cfg,
	[MNOC_SLV_MISC_XPU_CFG] = &slv_misc_xpu_cfg,
	[MNOC_SLV_VENUS_CFG] = &slv_venus_cfg,
	[MNOC_SLV_GRAPHICS_3D_CFG] = &slv_graphics_3d_cfg,
	[MNOC_SLV_MMSS_CLK_CFG] = &slv_mmss_clk_cfg,
	[MNOC_SLV_MMSS_CLK_XPU_CFG] = &slv_mmss_clk_xpu_cfg,
	[MNOC_SLV_MNOC_MPU_CFG] = &slv_mnoc_mpu_cfg,
	[MNOC_SLV_ONOC_MPU_CFG] = &slv_onoc_mpu_cfg,
	[MNOC_SLV_SERVICE_MNOC] = &slv_service_mnoc,
};

static struct msm8974_icc_desc msm8974_mnoc = {
	.nodes = msm8974_mnoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8974_mnoc_nodes),
};

DEFINE_QNODE(ocmem_noc_to_ocmem_vnoc, MSM8974_OCMEM_NOC_TO_OCMEM_VNOC, 16, 54, 78, MSM8974_OCMEM_SLV_OCMEM);
DEFINE_QNODE(mas_jpeg_ocmem, MSM8974_OCMEM_MAS_JPEG_OCMEM, 16, 13, -1);
DEFINE_QNODE(mas_mdp_ocmem, MSM8974_OCMEM_MAS_MDP_OCMEM, 16, 14, -1);
DEFINE_QNODE(mas_video_p0_ocmem, MSM8974_OCMEM_MAS_VIDEO_P0_OCMEM, 16, 15, -1);
DEFINE_QNODE(mas_video_p1_ocmem, MSM8974_OCMEM_MAS_VIDEO_P1_OCMEM, 16, 16, -1);
DEFINE_QNODE(mas_vfe_ocmem, MSM8974_OCMEM_MAS_VFE_OCMEM, 16, 17, -1);
DEFINE_QNODE(mas_cnoc_onoc_cfg, MSM8974_OCMEM_MAS_CNOC_ONOC_CFG, 16, 12, -1);
DEFINE_QNODE(slv_service_onoc, MSM8974_OCMEM_SLV_SERVICE_ONOC, 16, -1, 19);
DEFINE_QNODE(slv_ocmem, MSM8974_OCMEM_SLV_OCMEM, 16, -1, 18);

/* Virtual NoC is needed for connection to OCMEM */
DEFINE_QNODE(ocmem_vnoc_to_onoc, MSM8974_OCMEM_VNOC_TO_OCMEM_NOC, 16, 56, 79, MSM8974_OCMEM_NOC_TO_OCMEM_VNOC);
DEFINE_QNODE(ocmem_vnoc_to_snoc, MSM8974_OCMEM_VNOC_TO_SNOC, 8, 57, 80);
DEFINE_QNODE(mas_v_ocmem_gfx3d, MSM8974_OCMEM_VNOC_MAS_GFX3D, 8, 55, -1, MSM8974_OCMEM_VNOC_TO_OCMEM_NOC);

static struct msm8974_icc_node *msm8974_onoc_nodes[] = {
	[OCMEM_NOC_TO_OCMEM_VNOC] = &ocmem_noc_to_ocmem_vnoc,
	[OCMEM_MAS_JPEG_OCMEM] = &mas_jpeg_ocmem,
	[OCMEM_MAS_MDP_OCMEM] = &mas_mdp_ocmem,
	[OCMEM_MAS_VIDEO_P0_OCMEM] = &mas_video_p0_ocmem,
	[OCMEM_MAS_VIDEO_P1_OCMEM] = &mas_video_p1_ocmem,
	[OCMEM_MAS_VFE_OCMEM] = &mas_vfe_ocmem,
	[OCMEM_MAS_CNOC_ONOC_CFG] = &mas_cnoc_onoc_cfg,
	[OCMEM_SLV_SERVICE_ONOC] = &slv_service_onoc,
	[OCMEM_VNOC_TO_SNOC] = &ocmem_vnoc_to_snoc,
	[OCMEM_VNOC_TO_OCMEM_NOC] = &ocmem_vnoc_to_onoc,
	[OCMEM_VNOC_MAS_GFX3D] = &mas_v_ocmem_gfx3d,
	[OCMEM_SLV_OCMEM] = &slv_ocmem,
};

static struct msm8974_icc_desc msm8974_onoc = {
	.nodes = msm8974_onoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8974_onoc_nodes),
};

DEFINE_QNODE(mas_pnoc_cfg, MSM8974_PNOC_MAS_PNOC_CFG, 8, 43, -1);
DEFINE_QNODE(mas_sdcc_1, MSM8974_PNOC_MAS_SDCC_1, 8, 33, -1, MSM8974_PNOC_TO_SNOC);
DEFINE_QNODE(mas_sdcc_3, MSM8974_PNOC_MAS_SDCC_3, 8, 34, -1, MSM8974_PNOC_TO_SNOC);
DEFINE_QNODE(mas_sdcc_4, MSM8974_PNOC_MAS_SDCC_4, 8, 36, -1, MSM8974_PNOC_TO_SNOC);
DEFINE_QNODE(mas_sdcc_2, MSM8974_PNOC_MAS_SDCC_2, 8, 35, -1, MSM8974_PNOC_TO_SNOC);
DEFINE_QNODE(mas_tsif, MSM8974_PNOC_MAS_TSIF, 8, 37, -1, MSM8974_PNOC_TO_SNOC);
DEFINE_QNODE(mas_bam_dma, MSM8974_PNOC_MAS_BAM_DMA, 8, 38, -1);
DEFINE_QNODE(mas_blsp_2, MSM8974_PNOC_MAS_BLSP_2, 8, 39, -1, MSM8974_PNOC_TO_SNOC);
DEFINE_QNODE(mas_usb_hsic, MSM8974_PNOC_MAS_USB_HSIC, 8, 40, -1, MSM8974_PNOC_TO_SNOC);
DEFINE_QNODE(mas_blsp_1, MSM8974_PNOC_MAS_BLSP_1, 8, 41, -1, MSM8974_PNOC_TO_SNOC);
DEFINE_QNODE(mas_usb_hs, MSM8974_PNOC_MAS_USB_HS, 8, 42, -1, MSM8974_PNOC_TO_SNOC);
DEFINE_QNODE(pnoc_to_snoc, MSM8974_PNOC_TO_SNOC, 8, 44, 45, MSM8974_SNOC_TO_PNOC, MSM8974_PNOC_SLV_PRNG);
DEFINE_QNODE(slv_sdcc_1, MSM8974_PNOC_SLV_SDCC_1, 8, -1, 31);
DEFINE_QNODE(slv_sdcc_3, MSM8974_PNOC_SLV_SDCC_3, 8, -1, 32);
DEFINE_QNODE(slv_sdcc_2, MSM8974_PNOC_SLV_SDCC_2, 8, -1, 33);
DEFINE_QNODE(slv_sdcc_4, MSM8974_PNOC_SLV_SDCC_4, 8, -1, 34);
DEFINE_QNODE(slv_tsif, MSM8974_PNOC_SLV_TSIF, 8, -1, 35);
DEFINE_QNODE(slv_bam_dma, MSM8974_PNOC_SLV_BAM_DMA, 8, -1, 36);
DEFINE_QNODE(slv_blsp_2, MSM8974_PNOC_SLV_BLSP_2, 8, -1, 37);
DEFINE_QNODE(slv_usb_hsic, MSM8974_PNOC_SLV_USB_HSIC, 8, -1, 38);
DEFINE_QNODE(slv_blsp_1, MSM8974_PNOC_SLV_BLSP_1, 8, -1, 39);
DEFINE_QNODE(slv_usb_hs, MSM8974_PNOC_SLV_USB_HS, 8, -1, 40);
DEFINE_QNODE(slv_pdm, MSM8974_PNOC_SLV_PDM, 8, -1, 41);
DEFINE_QNODE(slv_periph_apu_cfg, MSM8974_PNOC_SLV_PERIPH_APU_CFG, 8, -1, 42);
DEFINE_QNODE(slv_pnoc_mpu_cfg, MSM8974_PNOC_SLV_PNOC_MPU_CFG, 8, -1, 43);
DEFINE_QNODE(slv_prng, MSM8974_PNOC_SLV_PRNG, 8, -1, 44, MSM8974_PNOC_TO_SNOC);
DEFINE_QNODE(slv_service_pnoc, MSM8974_PNOC_SLV_SERVICE_PNOC, 8, -1, 46);

static struct msm8974_icc_node *msm8974_pnoc_nodes[] = {
	[PNOC_MAS_PNOC_CFG] = &mas_pnoc_cfg,
	[PNOC_MAS_SDCC_1] = &mas_sdcc_1,
	[PNOC_MAS_SDCC_3] = &mas_sdcc_3,
	[PNOC_MAS_SDCC_4] = &mas_sdcc_4,
	[PNOC_MAS_SDCC_2] = &mas_sdcc_2,
	[PNOC_MAS_TSIF] = &mas_tsif,
	[PNOC_MAS_BAM_DMA] = &mas_bam_dma,
	[PNOC_MAS_BLSP_2] = &mas_blsp_2,
	[PNOC_MAS_USB_HSIC] = &mas_usb_hsic,
	[PNOC_MAS_BLSP_1] = &mas_blsp_1,
	[PNOC_MAS_USB_HS] = &mas_usb_hs,
	[PNOC_TO_SNOC] = &pnoc_to_snoc,
	[PNOC_SLV_SDCC_1] = &slv_sdcc_1,
	[PNOC_SLV_SDCC_3] = &slv_sdcc_3,
	[PNOC_SLV_SDCC_2] = &slv_sdcc_2,
	[PNOC_SLV_SDCC_4] = &slv_sdcc_4,
	[PNOC_SLV_TSIF] = &slv_tsif,
	[PNOC_SLV_BAM_DMA] = &slv_bam_dma,
	[PNOC_SLV_BLSP_2] = &slv_blsp_2,
	[PNOC_SLV_USB_HSIC] = &slv_usb_hsic,
	[PNOC_SLV_BLSP_1] = &slv_blsp_1,
	[PNOC_SLV_USB_HS] = &slv_usb_hs,
	[PNOC_SLV_PDM] = &slv_pdm,
	[PNOC_SLV_PERIPH_APU_CFG] = &slv_periph_apu_cfg,
	[PNOC_SLV_PNOC_MPU_CFG] = &slv_pnoc_mpu_cfg,
	[PNOC_SLV_PRNG] = &slv_prng,
	[PNOC_SLV_SERVICE_PNOC] = &slv_service_pnoc,
};

static struct msm8974_icc_desc msm8974_pnoc = {
	.nodes = msm8974_pnoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8974_pnoc_nodes),
};

DEFINE_QNODE(mas_lpass_ahb, MSM8974_SNOC_MAS_LPASS_AHB, 8, 18, -1);
DEFINE_QNODE(mas_qdss_bam, MSM8974_SNOC_MAS_QDSS_BAM, 8, 19, -1);
DEFINE_QNODE(mas_snoc_cfg, MSM8974_SNOC_MAS_SNOC_CFG, 8, 20, -1);
DEFINE_QNODE(snoc_to_bimc, MSM8974_SNOC_TO_BIMC, 8, 21, 24, MSM8974_BIMC_TO_SNOC);
DEFINE_QNODE(snoc_to_cnoc, MSM8974_SNOC_TO_CNOC, 8, 22, 25);
DEFINE_QNODE(snoc_to_pnoc, MSM8974_SNOC_TO_PNOC, 8, 29, 28, MSM8974_PNOC_TO_SNOC);
DEFINE_QNODE(snoc_to_ocmem_vnoc, MSM8974_SNOC_TO_OCMEM_VNOC, 8, 53, 77, MSM8974_OCMEM_VNOC_TO_OCMEM_NOC);
DEFINE_QNODE(mas_crypto_core0, MSM8974_SNOC_MAS_CRYPTO_CORE0, 8, 23, -1, MSM8974_SNOC_TO_BIMC);
DEFINE_QNODE(mas_crypto_core1, MSM8974_SNOC_MAS_CRYPTO_CORE1, 8, 24, -1);
DEFINE_QNODE(mas_lpass_proc, MSM8974_SNOC_MAS_LPASS_PROC, 8, 25, -1, MSM8974_SNOC_TO_OCMEM_VNOC);
DEFINE_QNODE(mas_mss, MSM8974_SNOC_MAS_MSS, 8, 26, -1);
DEFINE_QNODE(mas_mss_nav, MSM8974_SNOC_MAS_MSS_NAV, 8, 27, -1);
DEFINE_QNODE(mas_ocmem_dma, MSM8974_SNOC_MAS_OCMEM_DMA, 8, 28, -1);
DEFINE_QNODE(mas_wcss, MSM8974_SNOC_MAS_WCSS, 8, 30, -1);
DEFINE_QNODE(mas_qdss_etr, MSM8974_SNOC_MAS_QDSS_ETR, 8, 31, -1);
DEFINE_QNODE(mas_usb3, MSM8974_SNOC_MAS_USB3, 8, 32, -1, MSM8974_SNOC_TO_BIMC);
DEFINE_QNODE(slv_ampss, MSM8974_SNOC_SLV_AMPSS, 8, -1, 20);
DEFINE_QNODE(slv_lpass, MSM8974_SNOC_SLV_LPASS, 8, -1, 21);
DEFINE_QNODE(slv_usb3, MSM8974_SNOC_SLV_USB3, 8, -1, 22);
DEFINE_QNODE(slv_wcss, MSM8974_SNOC_SLV_WCSS, 8, -1, 23);
DEFINE_QNODE(slv_ocimem, MSM8974_SNOC_SLV_OCIMEM, 8, -1, 26);
DEFINE_QNODE(slv_snoc_ocmem, MSM8974_SNOC_SLV_SNOC_OCMEM, 8, -1, 27);
DEFINE_QNODE(slv_service_snoc, MSM8974_SNOC_SLV_SERVICE_SNOC, 8, -1, 29);
DEFINE_QNODE(slv_qdss_stm, MSM8974_SNOC_SLV_QDSS_STM, 8, -1, 30);

static struct msm8974_icc_node *msm8974_snoc_nodes[] = {
	[SNOC_MAS_LPASS_AHB] = &mas_lpass_ahb,
	[SNOC_MAS_QDSS_BAM] = &mas_qdss_bam,
	[SNOC_MAS_SNOC_CFG] = &mas_snoc_cfg,
	[SNOC_TO_BIMC] = &snoc_to_bimc,
	[SNOC_TO_CNOC] = &snoc_to_cnoc,
	[SNOC_TO_PNOC] = &snoc_to_pnoc,
	[SNOC_TO_OCMEM_VNOC] = &snoc_to_ocmem_vnoc,
	[SNOC_MAS_CRYPTO_CORE0] = &mas_crypto_core0,
	[SNOC_MAS_CRYPTO_CORE1] = &mas_crypto_core1,
	[SNOC_MAS_LPASS_PROC] = &mas_lpass_proc,
	[SNOC_MAS_MSS] = &mas_mss,
	[SNOC_MAS_MSS_NAV] = &mas_mss_nav,
	[SNOC_MAS_OCMEM_DMA] = &mas_ocmem_dma,
	[SNOC_MAS_WCSS] = &mas_wcss,
	[SNOC_MAS_QDSS_ETR] = &mas_qdss_etr,
	[SNOC_MAS_USB3] = &mas_usb3,
	[SNOC_SLV_AMPSS] = &slv_ampss,
	[SNOC_SLV_LPASS] = &slv_lpass,
	[SNOC_SLV_USB3] = &slv_usb3,
	[SNOC_SLV_WCSS] = &slv_wcss,
	[SNOC_SLV_OCIMEM] = &slv_ocimem,
	[SNOC_SLV_SNOC_OCMEM] = &slv_snoc_ocmem,
	[SNOC_SLV_SERVICE_SNOC] = &slv_service_snoc,
	[SNOC_SLV_QDSS_STM] = &slv_qdss_stm,
};

static struct msm8974_icc_desc msm8974_snoc = {
	.nodes = msm8974_snoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8974_snoc_nodes),
};

static void msm8974_icc_rpm_smd_send(struct device *dev, int rsc_type,
				     char *name, int id, u64 val)
{
	int ret;

	if (id == -1)
		return;

	/*
	 * Setting the bandwidth requests for some nodes fails and this same
	 * behavior occurs on the downstream MSM 3.4 kernel sources based on
	 * errors like this in that kernel:
	 *
	 *   msm_rpm_get_error_from_ack(): RPM NACK Unsupported resource
	 *   AXI: msm_bus_rpm_req(): RPM: Ack failed
	 *   AXI: msm_bus_rpm_commit_arb(): RPM: Req fail: mas:32, bw:240000000
	 *
	 * Since there's no publicly available documentation for this hardware,
	 * and the bandwidth for some nodes in the path can be set properly,
	 * let's not return an error.
	 */
	ret = qcom_icc_rpm_smd_send(QCOM_SMD_RPM_ACTIVE_STATE, rsc_type, id,
				    val);
	if (ret)
		dev_dbg(dev, "Cannot set bandwidth for node %s (%d): %d\n",
			name, id, ret);
}

static int msm8974_icc_set(struct icc_node *src, struct icc_node *dst)
{
	struct msm8974_icc_node *src_qn, *dst_qn;
	struct msm8974_icc_provider *qp;
	u64 sum_bw, max_peak_bw, rate;
	u32 agg_avg = 0, agg_peak = 0;
	struct icc_provider *provider;
	struct icc_node *n;
	int ret, i;

	src_qn = src->data;
	dst_qn = dst->data;
	provider = src->provider;
	qp = to_msm8974_icc_provider(provider);

	list_for_each_entry(n, &provider->nodes, node_list)
		provider->aggregate(n, 0, n->avg_bw, n->peak_bw,
				    &agg_avg, &agg_peak);

	sum_bw = icc_units_to_bps(agg_avg);
	max_peak_bw = icc_units_to_bps(agg_peak);

	/* Set bandwidth on source node */
	msm8974_icc_rpm_smd_send(provider->dev, RPM_BUS_MASTER_REQ,
				 src_qn->name, src_qn->mas_rpm_id, sum_bw);

	msm8974_icc_rpm_smd_send(provider->dev, RPM_BUS_SLAVE_REQ,
				 src_qn->name, src_qn->slv_rpm_id, sum_bw);

	/* Set bandwidth on destination node */
	msm8974_icc_rpm_smd_send(provider->dev, RPM_BUS_MASTER_REQ,
				 dst_qn->name, dst_qn->mas_rpm_id, sum_bw);

	msm8974_icc_rpm_smd_send(provider->dev, RPM_BUS_SLAVE_REQ,
				 dst_qn->name, dst_qn->slv_rpm_id, sum_bw);

	rate = max(sum_bw, max_peak_bw);

	do_div(rate, src_qn->buswidth);

	rate = min_t(u32, rate, INT_MAX);

	if (src_qn->rate == rate)
		return 0;

	for (i = 0; i < qp->num_clks; i++) {
		ret = clk_set_rate(qp->bus_clks[i].clk, rate);
		if (ret) {
			dev_err(provider->dev, "%s clk_set_rate error: %d\n",
				qp->bus_clks[i].id, ret);
			ret = 0;
		}
	}

	src_qn->rate = rate;

	return 0;
}

static int msm8974_get_bw(struct icc_node *node, u32 *avg, u32 *peak)
{
	*avg = 0;
	*peak = 0;

	return 0;
}

static int msm8974_icc_probe(struct platform_device *pdev)
{
	const struct msm8974_icc_desc *desc;
	struct msm8974_icc_node **qnodes;
	struct msm8974_icc_provider *qp;
	struct device *dev = &pdev->dev;
	struct icc_onecell_data *data;
	struct icc_provider *provider;
	struct icc_node *node;
	size_t num_nodes, i;
	int ret;

	/* wait for the RPM proxy */
	if (!qcom_icc_rpm_smd_available())
		return -EPROBE_DEFER;

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -EINVAL;

	qnodes = desc->nodes;
	num_nodes = desc->num_nodes;

	qp = devm_kzalloc(dev, sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return -ENOMEM;

	data = devm_kzalloc(dev, struct_size(data, nodes, num_nodes),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	qp->bus_clks = devm_kmemdup(dev, msm8974_icc_bus_clocks,
				    sizeof(msm8974_icc_bus_clocks), GFP_KERNEL);
	if (!qp->bus_clks)
		return -ENOMEM;

	qp->num_clks = ARRAY_SIZE(msm8974_icc_bus_clocks);
	ret = devm_clk_bulk_get(dev, qp->num_clks, qp->bus_clks);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(qp->num_clks, qp->bus_clks);
	if (ret)
		return ret;

	provider = &qp->provider;
	INIT_LIST_HEAD(&provider->nodes);
	provider->dev = dev;
	provider->set = msm8974_icc_set;
	provider->aggregate = icc_std_aggregate;
	provider->xlate = of_icc_xlate_onecell;
	provider->data = data;
	provider->get_bw = msm8974_get_bw;

	ret = icc_provider_add(provider);
	if (ret) {
		dev_err(dev, "error adding interconnect provider: %d\n", ret);
		goto err_disable_clks;
	}

	for (i = 0; i < num_nodes; i++) {
		size_t j;

		node = icc_node_create(qnodes[i]->id);
		if (IS_ERR(node)) {
			ret = PTR_ERR(node);
			goto err_del_icc;
		}

		node->name = qnodes[i]->name;
		node->data = qnodes[i];
		icc_node_add(node, provider);

		dev_dbg(dev, "registered node %s\n", node->name);

		/* populate links */
		for (j = 0; j < qnodes[i]->num_links; j++)
			icc_link_create(node, qnodes[i]->links[j]);

		data->nodes[i] = node;
	}
	data->num_nodes = num_nodes;

	platform_set_drvdata(pdev, qp);

	return 0;

err_del_icc:
	icc_nodes_remove(provider);
	icc_provider_del(provider);

err_disable_clks:
	clk_bulk_disable_unprepare(qp->num_clks, qp->bus_clks);

	return ret;
}

static int msm8974_icc_remove(struct platform_device *pdev)
{
	struct msm8974_icc_provider *qp = platform_get_drvdata(pdev);

	icc_nodes_remove(&qp->provider);
	clk_bulk_disable_unprepare(qp->num_clks, qp->bus_clks);
	return icc_provider_del(&qp->provider);
}

static const struct of_device_id msm8974_noc_of_match[] = {
	{ .compatible = "qcom,msm8974-bimc", .data = &msm8974_bimc},
	{ .compatible = "qcom,msm8974-cnoc", .data = &msm8974_cnoc},
	{ .compatible = "qcom,msm8974-mmssnoc", .data = &msm8974_mnoc},
	{ .compatible = "qcom,msm8974-ocmemnoc", .data = &msm8974_onoc},
	{ .compatible = "qcom,msm8974-pnoc", .data = &msm8974_pnoc},
	{ .compatible = "qcom,msm8974-snoc", .data = &msm8974_snoc},
	{ },
};
MODULE_DEVICE_TABLE(of, msm8974_noc_of_match);

static struct platform_driver msm8974_noc_driver = {
	.probe = msm8974_icc_probe,
	.remove = msm8974_icc_remove,
	.driver = {
		.name = "qnoc-msm8974",
		.of_match_table = msm8974_noc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(msm8974_noc_driver);
MODULE_DESCRIPTION("Qualcomm MSM8974 NoC driver");
MODULE_AUTHOR("Brian Masney <masneyb@onstation.org>");
MODULE_LICENSE("GPL v2");
