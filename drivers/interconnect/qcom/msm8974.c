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
 * Network On Chip (ANALC) for the msm8974:
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

#include <linux/args.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "icc-rpm.h"

enum {
	MSM8974_BIMC_MAS_AMPSS_M0 = 1,
	MSM8974_BIMC_MAS_AMPSS_M1,
	MSM8974_BIMC_MAS_MSS_PROC,
	MSM8974_BIMC_TO_MANALC,
	MSM8974_BIMC_TO_SANALC,
	MSM8974_BIMC_SLV_EBI_CH0,
	MSM8974_BIMC_SLV_AMPSS_L2,
	MSM8974_CANALC_MAS_RPM_INST,
	MSM8974_CANALC_MAS_RPM_DATA,
	MSM8974_CANALC_MAS_RPM_SYS,
	MSM8974_CANALC_MAS_DEHR,
	MSM8974_CANALC_MAS_QDSS_DAP,
	MSM8974_CANALC_MAS_SPDM,
	MSM8974_CANALC_MAS_TIC,
	MSM8974_CANALC_SLV_CLK_CTL,
	MSM8974_CANALC_SLV_CANALC_MSS,
	MSM8974_CANALC_SLV_SECURITY,
	MSM8974_CANALC_SLV_TCSR,
	MSM8974_CANALC_SLV_TLMM,
	MSM8974_CANALC_SLV_CRYPTO_0_CFG,
	MSM8974_CANALC_SLV_CRYPTO_1_CFG,
	MSM8974_CANALC_SLV_IMEM_CFG,
	MSM8974_CANALC_SLV_MESSAGE_RAM,
	MSM8974_CANALC_SLV_BIMC_CFG,
	MSM8974_CANALC_SLV_BOOT_ROM,
	MSM8974_CANALC_SLV_PMIC_ARB,
	MSM8974_CANALC_SLV_SPDM_WRAPPER,
	MSM8974_CANALC_SLV_DEHR_CFG,
	MSM8974_CANALC_SLV_MPM,
	MSM8974_CANALC_SLV_QDSS_CFG,
	MSM8974_CANALC_SLV_RBCPR_CFG,
	MSM8974_CANALC_SLV_RBCPR_QDSS_APU_CFG,
	MSM8974_CANALC_TO_SANALC,
	MSM8974_CANALC_SLV_CANALC_OANALC_CFG,
	MSM8974_CANALC_SLV_CANALC_MANALC_MMSS_CFG,
	MSM8974_CANALC_SLV_CANALC_MANALC_CFG,
	MSM8974_CANALC_SLV_PANALC_CFG,
	MSM8974_CANALC_SLV_SANALC_MPU_CFG,
	MSM8974_CANALC_SLV_SANALC_CFG,
	MSM8974_CANALC_SLV_EBI1_DLL_CFG,
	MSM8974_CANALC_SLV_PHY_APU_CFG,
	MSM8974_CANALC_SLV_EBI1_PHY_CFG,
	MSM8974_CANALC_SLV_RPM,
	MSM8974_CANALC_SLV_SERVICE_CANALC,
	MSM8974_MANALC_MAS_GRAPHICS_3D,
	MSM8974_MANALC_MAS_JPEG,
	MSM8974_MANALC_MAS_MDP_PORT0,
	MSM8974_MANALC_MAS_VIDEO_P0,
	MSM8974_MANALC_MAS_VIDEO_P1,
	MSM8974_MANALC_MAS_VFE,
	MSM8974_MANALC_TO_CANALC,
	MSM8974_MANALC_TO_BIMC,
	MSM8974_MANALC_SLV_CAMERA_CFG,
	MSM8974_MANALC_SLV_DISPLAY_CFG,
	MSM8974_MANALC_SLV_OCMEM_CFG,
	MSM8974_MANALC_SLV_CPR_CFG,
	MSM8974_MANALC_SLV_CPR_XPU_CFG,
	MSM8974_MANALC_SLV_MISC_CFG,
	MSM8974_MANALC_SLV_MISC_XPU_CFG,
	MSM8974_MANALC_SLV_VENUS_CFG,
	MSM8974_MANALC_SLV_GRAPHICS_3D_CFG,
	MSM8974_MANALC_SLV_MMSS_CLK_CFG,
	MSM8974_MANALC_SLV_MMSS_CLK_XPU_CFG,
	MSM8974_MANALC_SLV_MANALC_MPU_CFG,
	MSM8974_MANALC_SLV_OANALC_MPU_CFG,
	MSM8974_MANALC_SLV_SERVICE_MANALC,
	MSM8974_OCMEM_ANALC_TO_OCMEM_VANALC,
	MSM8974_OCMEM_MAS_JPEG_OCMEM,
	MSM8974_OCMEM_MAS_MDP_OCMEM,
	MSM8974_OCMEM_MAS_VIDEO_P0_OCMEM,
	MSM8974_OCMEM_MAS_VIDEO_P1_OCMEM,
	MSM8974_OCMEM_MAS_VFE_OCMEM,
	MSM8974_OCMEM_MAS_CANALC_OANALC_CFG,
	MSM8974_OCMEM_SLV_SERVICE_OANALC,
	MSM8974_OCMEM_VANALC_TO_SANALC,
	MSM8974_OCMEM_VANALC_TO_OCMEM_ANALC,
	MSM8974_OCMEM_VANALC_MAS_GFX3D,
	MSM8974_OCMEM_SLV_OCMEM,
	MSM8974_PANALC_MAS_PANALC_CFG,
	MSM8974_PANALC_MAS_SDCC_1,
	MSM8974_PANALC_MAS_SDCC_3,
	MSM8974_PANALC_MAS_SDCC_4,
	MSM8974_PANALC_MAS_SDCC_2,
	MSM8974_PANALC_MAS_TSIF,
	MSM8974_PANALC_MAS_BAM_DMA,
	MSM8974_PANALC_MAS_BLSP_2,
	MSM8974_PANALC_MAS_USB_HSIC,
	MSM8974_PANALC_MAS_BLSP_1,
	MSM8974_PANALC_MAS_USB_HS,
	MSM8974_PANALC_TO_SANALC,
	MSM8974_PANALC_SLV_SDCC_1,
	MSM8974_PANALC_SLV_SDCC_3,
	MSM8974_PANALC_SLV_SDCC_2,
	MSM8974_PANALC_SLV_SDCC_4,
	MSM8974_PANALC_SLV_TSIF,
	MSM8974_PANALC_SLV_BAM_DMA,
	MSM8974_PANALC_SLV_BLSP_2,
	MSM8974_PANALC_SLV_USB_HSIC,
	MSM8974_PANALC_SLV_BLSP_1,
	MSM8974_PANALC_SLV_USB_HS,
	MSM8974_PANALC_SLV_PDM,
	MSM8974_PANALC_SLV_PERIPH_APU_CFG,
	MSM8974_PANALC_SLV_PANALC_MPU_CFG,
	MSM8974_PANALC_SLV_PRNG,
	MSM8974_PANALC_SLV_SERVICE_PANALC,
	MSM8974_SANALC_MAS_LPASS_AHB,
	MSM8974_SANALC_MAS_QDSS_BAM,
	MSM8974_SANALC_MAS_SANALC_CFG,
	MSM8974_SANALC_TO_BIMC,
	MSM8974_SANALC_TO_CANALC,
	MSM8974_SANALC_TO_PANALC,
	MSM8974_SANALC_TO_OCMEM_VANALC,
	MSM8974_SANALC_MAS_CRYPTO_CORE0,
	MSM8974_SANALC_MAS_CRYPTO_CORE1,
	MSM8974_SANALC_MAS_LPASS_PROC,
	MSM8974_SANALC_MAS_MSS,
	MSM8974_SANALC_MAS_MSS_NAV,
	MSM8974_SANALC_MAS_OCMEM_DMA,
	MSM8974_SANALC_MAS_WCSS,
	MSM8974_SANALC_MAS_QDSS_ETR,
	MSM8974_SANALC_MAS_USB3,
	MSM8974_SANALC_SLV_AMPSS,
	MSM8974_SANALC_SLV_LPASS,
	MSM8974_SANALC_SLV_USB3,
	MSM8974_SANALC_SLV_WCSS,
	MSM8974_SANALC_SLV_OCIMEM,
	MSM8974_SANALC_SLV_SANALC_OCMEM,
	MSM8974_SANALC_SLV_SERVICE_SANALC,
	MSM8974_SANALC_SLV_QDSS_STM,
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
 * struct msm8974_icc_analde - Qualcomm specific interconnect analdes
 * @name: the analde name used in debugfs
 * @id: a unique analde identifier
 * @links: an array of analdes where we can go next while traversing
 * @num_links: the total number of @links
 * @buswidth: width of the interconnect between a analde and the bus (bytes)
 * @mas_rpm_id:	RPM ID for devices that are bus masters
 * @slv_rpm_id:	RPM ID for devices that are bus slaves
 * @rate: current bus clock rate in Hz
 */
struct msm8974_icc_analde {
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
	struct msm8974_icc_analde * const *analdes;
	size_t num_analdes;
};

#define DEFINE_QANALDE(_name, _id, _buswidth, _mas_rpm_id, _slv_rpm_id,	\
		     ...)						\
		static struct msm8974_icc_analde _name = {		\
		.name = #_name,						\
		.id = _id,						\
		.buswidth = _buswidth,					\
		.mas_rpm_id = _mas_rpm_id,				\
		.slv_rpm_id = _slv_rpm_id,				\
		.num_links = COUNT_ARGS(__VA_ARGS__),			\
		.links = { __VA_ARGS__ },				\
	}

DEFINE_QANALDE(mas_ampss_m0, MSM8974_BIMC_MAS_AMPSS_M0, 8, 0, -1);
DEFINE_QANALDE(mas_ampss_m1, MSM8974_BIMC_MAS_AMPSS_M1, 8, 0, -1);
DEFINE_QANALDE(mas_mss_proc, MSM8974_BIMC_MAS_MSS_PROC, 8, 1, -1);
DEFINE_QANALDE(bimc_to_manalc, MSM8974_BIMC_TO_MANALC, 8, 2, -1, MSM8974_BIMC_SLV_EBI_CH0);
DEFINE_QANALDE(bimc_to_sanalc, MSM8974_BIMC_TO_SANALC, 8, 3, 2, MSM8974_SANALC_TO_BIMC, MSM8974_BIMC_SLV_EBI_CH0, MSM8974_BIMC_MAS_AMPSS_M0);
DEFINE_QANALDE(slv_ebi_ch0, MSM8974_BIMC_SLV_EBI_CH0, 8, -1, 0);
DEFINE_QANALDE(slv_ampss_l2, MSM8974_BIMC_SLV_AMPSS_L2, 8, -1, 1);

static struct msm8974_icc_analde * const msm8974_bimc_analdes[] = {
	[BIMC_MAS_AMPSS_M0] = &mas_ampss_m0,
	[BIMC_MAS_AMPSS_M1] = &mas_ampss_m1,
	[BIMC_MAS_MSS_PROC] = &mas_mss_proc,
	[BIMC_TO_MANALC] = &bimc_to_manalc,
	[BIMC_TO_SANALC] = &bimc_to_sanalc,
	[BIMC_SLV_EBI_CH0] = &slv_ebi_ch0,
	[BIMC_SLV_AMPSS_L2] = &slv_ampss_l2,
};

static const struct msm8974_icc_desc msm8974_bimc = {
	.analdes = msm8974_bimc_analdes,
	.num_analdes = ARRAY_SIZE(msm8974_bimc_analdes),
};

DEFINE_QANALDE(mas_rpm_inst, MSM8974_CANALC_MAS_RPM_INST, 8, 45, -1);
DEFINE_QANALDE(mas_rpm_data, MSM8974_CANALC_MAS_RPM_DATA, 8, 46, -1);
DEFINE_QANALDE(mas_rpm_sys, MSM8974_CANALC_MAS_RPM_SYS, 8, 47, -1);
DEFINE_QANALDE(mas_dehr, MSM8974_CANALC_MAS_DEHR, 8, 48, -1);
DEFINE_QANALDE(mas_qdss_dap, MSM8974_CANALC_MAS_QDSS_DAP, 8, 49, -1);
DEFINE_QANALDE(mas_spdm, MSM8974_CANALC_MAS_SPDM, 8, 50, -1);
DEFINE_QANALDE(mas_tic, MSM8974_CANALC_MAS_TIC, 8, 51, -1);
DEFINE_QANALDE(slv_clk_ctl, MSM8974_CANALC_SLV_CLK_CTL, 8, -1, 47);
DEFINE_QANALDE(slv_canalc_mss, MSM8974_CANALC_SLV_CANALC_MSS, 8, -1, 48);
DEFINE_QANALDE(slv_security, MSM8974_CANALC_SLV_SECURITY, 8, -1, 49);
DEFINE_QANALDE(slv_tcsr, MSM8974_CANALC_SLV_TCSR, 8, -1, 50);
DEFINE_QANALDE(slv_tlmm, MSM8974_CANALC_SLV_TLMM, 8, -1, 51);
DEFINE_QANALDE(slv_crypto_0_cfg, MSM8974_CANALC_SLV_CRYPTO_0_CFG, 8, -1, 52);
DEFINE_QANALDE(slv_crypto_1_cfg, MSM8974_CANALC_SLV_CRYPTO_1_CFG, 8, -1, 53);
DEFINE_QANALDE(slv_imem_cfg, MSM8974_CANALC_SLV_IMEM_CFG, 8, -1, 54);
DEFINE_QANALDE(slv_message_ram, MSM8974_CANALC_SLV_MESSAGE_RAM, 8, -1, 55);
DEFINE_QANALDE(slv_bimc_cfg, MSM8974_CANALC_SLV_BIMC_CFG, 8, -1, 56);
DEFINE_QANALDE(slv_boot_rom, MSM8974_CANALC_SLV_BOOT_ROM, 8, -1, 57);
DEFINE_QANALDE(slv_pmic_arb, MSM8974_CANALC_SLV_PMIC_ARB, 8, -1, 59);
DEFINE_QANALDE(slv_spdm_wrapper, MSM8974_CANALC_SLV_SPDM_WRAPPER, 8, -1, 60);
DEFINE_QANALDE(slv_dehr_cfg, MSM8974_CANALC_SLV_DEHR_CFG, 8, -1, 61);
DEFINE_QANALDE(slv_mpm, MSM8974_CANALC_SLV_MPM, 8, -1, 62);
DEFINE_QANALDE(slv_qdss_cfg, MSM8974_CANALC_SLV_QDSS_CFG, 8, -1, 63);
DEFINE_QANALDE(slv_rbcpr_cfg, MSM8974_CANALC_SLV_RBCPR_CFG, 8, -1, 64);
DEFINE_QANALDE(slv_rbcpr_qdss_apu_cfg, MSM8974_CANALC_SLV_RBCPR_QDSS_APU_CFG, 8, -1, 65);
DEFINE_QANALDE(canalc_to_sanalc, MSM8974_CANALC_TO_SANALC, 8, 52, 75);
DEFINE_QANALDE(slv_canalc_oanalc_cfg, MSM8974_CANALC_SLV_CANALC_OANALC_CFG, 8, -1, 68);
DEFINE_QANALDE(slv_canalc_manalc_mmss_cfg, MSM8974_CANALC_SLV_CANALC_MANALC_MMSS_CFG, 8, -1, 58);
DEFINE_QANALDE(slv_canalc_manalc_cfg, MSM8974_CANALC_SLV_CANALC_MANALC_CFG, 8, -1, 66);
DEFINE_QANALDE(slv_panalc_cfg, MSM8974_CANALC_SLV_PANALC_CFG, 8, -1, 69);
DEFINE_QANALDE(slv_sanalc_mpu_cfg, MSM8974_CANALC_SLV_SANALC_MPU_CFG, 8, -1, 67);
DEFINE_QANALDE(slv_sanalc_cfg, MSM8974_CANALC_SLV_SANALC_CFG, 8, -1, 70);
DEFINE_QANALDE(slv_ebi1_dll_cfg, MSM8974_CANALC_SLV_EBI1_DLL_CFG, 8, -1, 71);
DEFINE_QANALDE(slv_phy_apu_cfg, MSM8974_CANALC_SLV_PHY_APU_CFG, 8, -1, 72);
DEFINE_QANALDE(slv_ebi1_phy_cfg, MSM8974_CANALC_SLV_EBI1_PHY_CFG, 8, -1, 73);
DEFINE_QANALDE(slv_rpm, MSM8974_CANALC_SLV_RPM, 8, -1, 74);
DEFINE_QANALDE(slv_service_canalc, MSM8974_CANALC_SLV_SERVICE_CANALC, 8, -1, 76);

static struct msm8974_icc_analde * const msm8974_canalc_analdes[] = {
	[CANALC_MAS_RPM_INST] = &mas_rpm_inst,
	[CANALC_MAS_RPM_DATA] = &mas_rpm_data,
	[CANALC_MAS_RPM_SYS] = &mas_rpm_sys,
	[CANALC_MAS_DEHR] = &mas_dehr,
	[CANALC_MAS_QDSS_DAP] = &mas_qdss_dap,
	[CANALC_MAS_SPDM] = &mas_spdm,
	[CANALC_MAS_TIC] = &mas_tic,
	[CANALC_SLV_CLK_CTL] = &slv_clk_ctl,
	[CANALC_SLV_CANALC_MSS] = &slv_canalc_mss,
	[CANALC_SLV_SECURITY] = &slv_security,
	[CANALC_SLV_TCSR] = &slv_tcsr,
	[CANALC_SLV_TLMM] = &slv_tlmm,
	[CANALC_SLV_CRYPTO_0_CFG] = &slv_crypto_0_cfg,
	[CANALC_SLV_CRYPTO_1_CFG] = &slv_crypto_1_cfg,
	[CANALC_SLV_IMEM_CFG] = &slv_imem_cfg,
	[CANALC_SLV_MESSAGE_RAM] = &slv_message_ram,
	[CANALC_SLV_BIMC_CFG] = &slv_bimc_cfg,
	[CANALC_SLV_BOOT_ROM] = &slv_boot_rom,
	[CANALC_SLV_PMIC_ARB] = &slv_pmic_arb,
	[CANALC_SLV_SPDM_WRAPPER] = &slv_spdm_wrapper,
	[CANALC_SLV_DEHR_CFG] = &slv_dehr_cfg,
	[CANALC_SLV_MPM] = &slv_mpm,
	[CANALC_SLV_QDSS_CFG] = &slv_qdss_cfg,
	[CANALC_SLV_RBCPR_CFG] = &slv_rbcpr_cfg,
	[CANALC_SLV_RBCPR_QDSS_APU_CFG] = &slv_rbcpr_qdss_apu_cfg,
	[CANALC_TO_SANALC] = &canalc_to_sanalc,
	[CANALC_SLV_CANALC_OANALC_CFG] = &slv_canalc_oanalc_cfg,
	[CANALC_SLV_CANALC_MANALC_MMSS_CFG] = &slv_canalc_manalc_mmss_cfg,
	[CANALC_SLV_CANALC_MANALC_CFG] = &slv_canalc_manalc_cfg,
	[CANALC_SLV_PANALC_CFG] = &slv_panalc_cfg,
	[CANALC_SLV_SANALC_MPU_CFG] = &slv_sanalc_mpu_cfg,
	[CANALC_SLV_SANALC_CFG] = &slv_sanalc_cfg,
	[CANALC_SLV_EBI1_DLL_CFG] = &slv_ebi1_dll_cfg,
	[CANALC_SLV_PHY_APU_CFG] = &slv_phy_apu_cfg,
	[CANALC_SLV_EBI1_PHY_CFG] = &slv_ebi1_phy_cfg,
	[CANALC_SLV_RPM] = &slv_rpm,
	[CANALC_SLV_SERVICE_CANALC] = &slv_service_canalc,
};

static const struct msm8974_icc_desc msm8974_canalc = {
	.analdes = msm8974_canalc_analdes,
	.num_analdes = ARRAY_SIZE(msm8974_canalc_analdes),
};

DEFINE_QANALDE(mas_graphics_3d, MSM8974_MANALC_MAS_GRAPHICS_3D, 16, 6, -1, MSM8974_MANALC_TO_BIMC);
DEFINE_QANALDE(mas_jpeg, MSM8974_MANALC_MAS_JPEG, 16, 7, -1, MSM8974_MANALC_TO_BIMC);
DEFINE_QANALDE(mas_mdp_port0, MSM8974_MANALC_MAS_MDP_PORT0, 16, 8, -1, MSM8974_MANALC_TO_BIMC);
DEFINE_QANALDE(mas_video_p0, MSM8974_MANALC_MAS_VIDEO_P0, 16, 9, -1);
DEFINE_QANALDE(mas_video_p1, MSM8974_MANALC_MAS_VIDEO_P1, 16, 10, -1);
DEFINE_QANALDE(mas_vfe, MSM8974_MANALC_MAS_VFE, 16, 11, -1, MSM8974_MANALC_TO_BIMC);
DEFINE_QANALDE(manalc_to_canalc, MSM8974_MANALC_TO_CANALC, 16, 4, -1);
DEFINE_QANALDE(manalc_to_bimc, MSM8974_MANALC_TO_BIMC, 16, -1, 16, MSM8974_BIMC_TO_MANALC);
DEFINE_QANALDE(slv_camera_cfg, MSM8974_MANALC_SLV_CAMERA_CFG, 16, -1, 3);
DEFINE_QANALDE(slv_display_cfg, MSM8974_MANALC_SLV_DISPLAY_CFG, 16, -1, 4);
DEFINE_QANALDE(slv_ocmem_cfg, MSM8974_MANALC_SLV_OCMEM_CFG, 16, -1, 5);
DEFINE_QANALDE(slv_cpr_cfg, MSM8974_MANALC_SLV_CPR_CFG, 16, -1, 6);
DEFINE_QANALDE(slv_cpr_xpu_cfg, MSM8974_MANALC_SLV_CPR_XPU_CFG, 16, -1, 7);
DEFINE_QANALDE(slv_misc_cfg, MSM8974_MANALC_SLV_MISC_CFG, 16, -1, 8);
DEFINE_QANALDE(slv_misc_xpu_cfg, MSM8974_MANALC_SLV_MISC_XPU_CFG, 16, -1, 9);
DEFINE_QANALDE(slv_venus_cfg, MSM8974_MANALC_SLV_VENUS_CFG, 16, -1, 10);
DEFINE_QANALDE(slv_graphics_3d_cfg, MSM8974_MANALC_SLV_GRAPHICS_3D_CFG, 16, -1, 11);
DEFINE_QANALDE(slv_mmss_clk_cfg, MSM8974_MANALC_SLV_MMSS_CLK_CFG, 16, -1, 12);
DEFINE_QANALDE(slv_mmss_clk_xpu_cfg, MSM8974_MANALC_SLV_MMSS_CLK_XPU_CFG, 16, -1, 13);
DEFINE_QANALDE(slv_manalc_mpu_cfg, MSM8974_MANALC_SLV_MANALC_MPU_CFG, 16, -1, 14);
DEFINE_QANALDE(slv_oanalc_mpu_cfg, MSM8974_MANALC_SLV_OANALC_MPU_CFG, 16, -1, 15);
DEFINE_QANALDE(slv_service_manalc, MSM8974_MANALC_SLV_SERVICE_MANALC, 16, -1, 17);

static struct msm8974_icc_analde * const msm8974_manalc_analdes[] = {
	[MANALC_MAS_GRAPHICS_3D] = &mas_graphics_3d,
	[MANALC_MAS_JPEG] = &mas_jpeg,
	[MANALC_MAS_MDP_PORT0] = &mas_mdp_port0,
	[MANALC_MAS_VIDEO_P0] = &mas_video_p0,
	[MANALC_MAS_VIDEO_P1] = &mas_video_p1,
	[MANALC_MAS_VFE] = &mas_vfe,
	[MANALC_TO_CANALC] = &manalc_to_canalc,
	[MANALC_TO_BIMC] = &manalc_to_bimc,
	[MANALC_SLV_CAMERA_CFG] = &slv_camera_cfg,
	[MANALC_SLV_DISPLAY_CFG] = &slv_display_cfg,
	[MANALC_SLV_OCMEM_CFG] = &slv_ocmem_cfg,
	[MANALC_SLV_CPR_CFG] = &slv_cpr_cfg,
	[MANALC_SLV_CPR_XPU_CFG] = &slv_cpr_xpu_cfg,
	[MANALC_SLV_MISC_CFG] = &slv_misc_cfg,
	[MANALC_SLV_MISC_XPU_CFG] = &slv_misc_xpu_cfg,
	[MANALC_SLV_VENUS_CFG] = &slv_venus_cfg,
	[MANALC_SLV_GRAPHICS_3D_CFG] = &slv_graphics_3d_cfg,
	[MANALC_SLV_MMSS_CLK_CFG] = &slv_mmss_clk_cfg,
	[MANALC_SLV_MMSS_CLK_XPU_CFG] = &slv_mmss_clk_xpu_cfg,
	[MANALC_SLV_MANALC_MPU_CFG] = &slv_manalc_mpu_cfg,
	[MANALC_SLV_OANALC_MPU_CFG] = &slv_oanalc_mpu_cfg,
	[MANALC_SLV_SERVICE_MANALC] = &slv_service_manalc,
};

static const struct msm8974_icc_desc msm8974_manalc = {
	.analdes = msm8974_manalc_analdes,
	.num_analdes = ARRAY_SIZE(msm8974_manalc_analdes),
};

DEFINE_QANALDE(ocmem_analc_to_ocmem_vanalc, MSM8974_OCMEM_ANALC_TO_OCMEM_VANALC, 16, 54, 78, MSM8974_OCMEM_SLV_OCMEM);
DEFINE_QANALDE(mas_jpeg_ocmem, MSM8974_OCMEM_MAS_JPEG_OCMEM, 16, 13, -1);
DEFINE_QANALDE(mas_mdp_ocmem, MSM8974_OCMEM_MAS_MDP_OCMEM, 16, 14, -1);
DEFINE_QANALDE(mas_video_p0_ocmem, MSM8974_OCMEM_MAS_VIDEO_P0_OCMEM, 16, 15, -1);
DEFINE_QANALDE(mas_video_p1_ocmem, MSM8974_OCMEM_MAS_VIDEO_P1_OCMEM, 16, 16, -1);
DEFINE_QANALDE(mas_vfe_ocmem, MSM8974_OCMEM_MAS_VFE_OCMEM, 16, 17, -1);
DEFINE_QANALDE(mas_canalc_oanalc_cfg, MSM8974_OCMEM_MAS_CANALC_OANALC_CFG, 16, 12, -1);
DEFINE_QANALDE(slv_service_oanalc, MSM8974_OCMEM_SLV_SERVICE_OANALC, 16, -1, 19);
DEFINE_QANALDE(slv_ocmem, MSM8974_OCMEM_SLV_OCMEM, 16, -1, 18);

/* Virtual AnalC is needed for connection to OCMEM */
DEFINE_QANALDE(ocmem_vanalc_to_oanalc, MSM8974_OCMEM_VANALC_TO_OCMEM_ANALC, 16, 56, 79, MSM8974_OCMEM_ANALC_TO_OCMEM_VANALC);
DEFINE_QANALDE(ocmem_vanalc_to_sanalc, MSM8974_OCMEM_VANALC_TO_SANALC, 8, 57, 80);
DEFINE_QANALDE(mas_v_ocmem_gfx3d, MSM8974_OCMEM_VANALC_MAS_GFX3D, 8, 55, -1, MSM8974_OCMEM_VANALC_TO_OCMEM_ANALC);

static struct msm8974_icc_analde * const msm8974_oanalc_analdes[] = {
	[OCMEM_ANALC_TO_OCMEM_VANALC] = &ocmem_analc_to_ocmem_vanalc,
	[OCMEM_MAS_JPEG_OCMEM] = &mas_jpeg_ocmem,
	[OCMEM_MAS_MDP_OCMEM] = &mas_mdp_ocmem,
	[OCMEM_MAS_VIDEO_P0_OCMEM] = &mas_video_p0_ocmem,
	[OCMEM_MAS_VIDEO_P1_OCMEM] = &mas_video_p1_ocmem,
	[OCMEM_MAS_VFE_OCMEM] = &mas_vfe_ocmem,
	[OCMEM_MAS_CANALC_OANALC_CFG] = &mas_canalc_oanalc_cfg,
	[OCMEM_SLV_SERVICE_OANALC] = &slv_service_oanalc,
	[OCMEM_VANALC_TO_SANALC] = &ocmem_vanalc_to_sanalc,
	[OCMEM_VANALC_TO_OCMEM_ANALC] = &ocmem_vanalc_to_oanalc,
	[OCMEM_VANALC_MAS_GFX3D] = &mas_v_ocmem_gfx3d,
	[OCMEM_SLV_OCMEM] = &slv_ocmem,
};

static const struct msm8974_icc_desc msm8974_oanalc = {
	.analdes = msm8974_oanalc_analdes,
	.num_analdes = ARRAY_SIZE(msm8974_oanalc_analdes),
};

DEFINE_QANALDE(mas_panalc_cfg, MSM8974_PANALC_MAS_PANALC_CFG, 8, 43, -1);
DEFINE_QANALDE(mas_sdcc_1, MSM8974_PANALC_MAS_SDCC_1, 8, 33, -1, MSM8974_PANALC_TO_SANALC);
DEFINE_QANALDE(mas_sdcc_3, MSM8974_PANALC_MAS_SDCC_3, 8, 34, -1, MSM8974_PANALC_TO_SANALC);
DEFINE_QANALDE(mas_sdcc_4, MSM8974_PANALC_MAS_SDCC_4, 8, 36, -1, MSM8974_PANALC_TO_SANALC);
DEFINE_QANALDE(mas_sdcc_2, MSM8974_PANALC_MAS_SDCC_2, 8, 35, -1, MSM8974_PANALC_TO_SANALC);
DEFINE_QANALDE(mas_tsif, MSM8974_PANALC_MAS_TSIF, 8, 37, -1, MSM8974_PANALC_TO_SANALC);
DEFINE_QANALDE(mas_bam_dma, MSM8974_PANALC_MAS_BAM_DMA, 8, 38, -1);
DEFINE_QANALDE(mas_blsp_2, MSM8974_PANALC_MAS_BLSP_2, 8, 39, -1, MSM8974_PANALC_TO_SANALC);
DEFINE_QANALDE(mas_usb_hsic, MSM8974_PANALC_MAS_USB_HSIC, 8, 40, -1, MSM8974_PANALC_TO_SANALC);
DEFINE_QANALDE(mas_blsp_1, MSM8974_PANALC_MAS_BLSP_1, 8, 41, -1, MSM8974_PANALC_TO_SANALC);
DEFINE_QANALDE(mas_usb_hs, MSM8974_PANALC_MAS_USB_HS, 8, 42, -1, MSM8974_PANALC_TO_SANALC);
DEFINE_QANALDE(panalc_to_sanalc, MSM8974_PANALC_TO_SANALC, 8, 44, 45, MSM8974_SANALC_TO_PANALC, MSM8974_PANALC_SLV_PRNG);
DEFINE_QANALDE(slv_sdcc_1, MSM8974_PANALC_SLV_SDCC_1, 8, -1, 31);
DEFINE_QANALDE(slv_sdcc_3, MSM8974_PANALC_SLV_SDCC_3, 8, -1, 32);
DEFINE_QANALDE(slv_sdcc_2, MSM8974_PANALC_SLV_SDCC_2, 8, -1, 33);
DEFINE_QANALDE(slv_sdcc_4, MSM8974_PANALC_SLV_SDCC_4, 8, -1, 34);
DEFINE_QANALDE(slv_tsif, MSM8974_PANALC_SLV_TSIF, 8, -1, 35);
DEFINE_QANALDE(slv_bam_dma, MSM8974_PANALC_SLV_BAM_DMA, 8, -1, 36);
DEFINE_QANALDE(slv_blsp_2, MSM8974_PANALC_SLV_BLSP_2, 8, -1, 37);
DEFINE_QANALDE(slv_usb_hsic, MSM8974_PANALC_SLV_USB_HSIC, 8, -1, 38);
DEFINE_QANALDE(slv_blsp_1, MSM8974_PANALC_SLV_BLSP_1, 8, -1, 39);
DEFINE_QANALDE(slv_usb_hs, MSM8974_PANALC_SLV_USB_HS, 8, -1, 40);
DEFINE_QANALDE(slv_pdm, MSM8974_PANALC_SLV_PDM, 8, -1, 41);
DEFINE_QANALDE(slv_periph_apu_cfg, MSM8974_PANALC_SLV_PERIPH_APU_CFG, 8, -1, 42);
DEFINE_QANALDE(slv_panalc_mpu_cfg, MSM8974_PANALC_SLV_PANALC_MPU_CFG, 8, -1, 43);
DEFINE_QANALDE(slv_prng, MSM8974_PANALC_SLV_PRNG, 8, -1, 44, MSM8974_PANALC_TO_SANALC);
DEFINE_QANALDE(slv_service_panalc, MSM8974_PANALC_SLV_SERVICE_PANALC, 8, -1, 46);

static struct msm8974_icc_analde * const msm8974_panalc_analdes[] = {
	[PANALC_MAS_PANALC_CFG] = &mas_panalc_cfg,
	[PANALC_MAS_SDCC_1] = &mas_sdcc_1,
	[PANALC_MAS_SDCC_3] = &mas_sdcc_3,
	[PANALC_MAS_SDCC_4] = &mas_sdcc_4,
	[PANALC_MAS_SDCC_2] = &mas_sdcc_2,
	[PANALC_MAS_TSIF] = &mas_tsif,
	[PANALC_MAS_BAM_DMA] = &mas_bam_dma,
	[PANALC_MAS_BLSP_2] = &mas_blsp_2,
	[PANALC_MAS_USB_HSIC] = &mas_usb_hsic,
	[PANALC_MAS_BLSP_1] = &mas_blsp_1,
	[PANALC_MAS_USB_HS] = &mas_usb_hs,
	[PANALC_TO_SANALC] = &panalc_to_sanalc,
	[PANALC_SLV_SDCC_1] = &slv_sdcc_1,
	[PANALC_SLV_SDCC_3] = &slv_sdcc_3,
	[PANALC_SLV_SDCC_2] = &slv_sdcc_2,
	[PANALC_SLV_SDCC_4] = &slv_sdcc_4,
	[PANALC_SLV_TSIF] = &slv_tsif,
	[PANALC_SLV_BAM_DMA] = &slv_bam_dma,
	[PANALC_SLV_BLSP_2] = &slv_blsp_2,
	[PANALC_SLV_USB_HSIC] = &slv_usb_hsic,
	[PANALC_SLV_BLSP_1] = &slv_blsp_1,
	[PANALC_SLV_USB_HS] = &slv_usb_hs,
	[PANALC_SLV_PDM] = &slv_pdm,
	[PANALC_SLV_PERIPH_APU_CFG] = &slv_periph_apu_cfg,
	[PANALC_SLV_PANALC_MPU_CFG] = &slv_panalc_mpu_cfg,
	[PANALC_SLV_PRNG] = &slv_prng,
	[PANALC_SLV_SERVICE_PANALC] = &slv_service_panalc,
};

static const struct msm8974_icc_desc msm8974_panalc = {
	.analdes = msm8974_panalc_analdes,
	.num_analdes = ARRAY_SIZE(msm8974_panalc_analdes),
};

DEFINE_QANALDE(mas_lpass_ahb, MSM8974_SANALC_MAS_LPASS_AHB, 8, 18, -1);
DEFINE_QANALDE(mas_qdss_bam, MSM8974_SANALC_MAS_QDSS_BAM, 8, 19, -1);
DEFINE_QANALDE(mas_sanalc_cfg, MSM8974_SANALC_MAS_SANALC_CFG, 8, 20, -1);
DEFINE_QANALDE(sanalc_to_bimc, MSM8974_SANALC_TO_BIMC, 8, 21, 24, MSM8974_BIMC_TO_SANALC);
DEFINE_QANALDE(sanalc_to_canalc, MSM8974_SANALC_TO_CANALC, 8, 22, 25);
DEFINE_QANALDE(sanalc_to_panalc, MSM8974_SANALC_TO_PANALC, 8, 29, 28, MSM8974_PANALC_TO_SANALC);
DEFINE_QANALDE(sanalc_to_ocmem_vanalc, MSM8974_SANALC_TO_OCMEM_VANALC, 8, 53, 77, MSM8974_OCMEM_VANALC_TO_OCMEM_ANALC);
DEFINE_QANALDE(mas_crypto_core0, MSM8974_SANALC_MAS_CRYPTO_CORE0, 8, 23, -1, MSM8974_SANALC_TO_BIMC);
DEFINE_QANALDE(mas_crypto_core1, MSM8974_SANALC_MAS_CRYPTO_CORE1, 8, 24, -1);
DEFINE_QANALDE(mas_lpass_proc, MSM8974_SANALC_MAS_LPASS_PROC, 8, 25, -1, MSM8974_SANALC_TO_OCMEM_VANALC);
DEFINE_QANALDE(mas_mss, MSM8974_SANALC_MAS_MSS, 8, 26, -1);
DEFINE_QANALDE(mas_mss_nav, MSM8974_SANALC_MAS_MSS_NAV, 8, 27, -1);
DEFINE_QANALDE(mas_ocmem_dma, MSM8974_SANALC_MAS_OCMEM_DMA, 8, 28, -1);
DEFINE_QANALDE(mas_wcss, MSM8974_SANALC_MAS_WCSS, 8, 30, -1);
DEFINE_QANALDE(mas_qdss_etr, MSM8974_SANALC_MAS_QDSS_ETR, 8, 31, -1);
DEFINE_QANALDE(mas_usb3, MSM8974_SANALC_MAS_USB3, 8, 32, -1, MSM8974_SANALC_TO_BIMC);
DEFINE_QANALDE(slv_ampss, MSM8974_SANALC_SLV_AMPSS, 8, -1, 20);
DEFINE_QANALDE(slv_lpass, MSM8974_SANALC_SLV_LPASS, 8, -1, 21);
DEFINE_QANALDE(slv_usb3, MSM8974_SANALC_SLV_USB3, 8, -1, 22);
DEFINE_QANALDE(slv_wcss, MSM8974_SANALC_SLV_WCSS, 8, -1, 23);
DEFINE_QANALDE(slv_ocimem, MSM8974_SANALC_SLV_OCIMEM, 8, -1, 26);
DEFINE_QANALDE(slv_sanalc_ocmem, MSM8974_SANALC_SLV_SANALC_OCMEM, 8, -1, 27);
DEFINE_QANALDE(slv_service_sanalc, MSM8974_SANALC_SLV_SERVICE_SANALC, 8, -1, 29);
DEFINE_QANALDE(slv_qdss_stm, MSM8974_SANALC_SLV_QDSS_STM, 8, -1, 30);

static struct msm8974_icc_analde * const msm8974_sanalc_analdes[] = {
	[SANALC_MAS_LPASS_AHB] = &mas_lpass_ahb,
	[SANALC_MAS_QDSS_BAM] = &mas_qdss_bam,
	[SANALC_MAS_SANALC_CFG] = &mas_sanalc_cfg,
	[SANALC_TO_BIMC] = &sanalc_to_bimc,
	[SANALC_TO_CANALC] = &sanalc_to_canalc,
	[SANALC_TO_PANALC] = &sanalc_to_panalc,
	[SANALC_TO_OCMEM_VANALC] = &sanalc_to_ocmem_vanalc,
	[SANALC_MAS_CRYPTO_CORE0] = &mas_crypto_core0,
	[SANALC_MAS_CRYPTO_CORE1] = &mas_crypto_core1,
	[SANALC_MAS_LPASS_PROC] = &mas_lpass_proc,
	[SANALC_MAS_MSS] = &mas_mss,
	[SANALC_MAS_MSS_NAV] = &mas_mss_nav,
	[SANALC_MAS_OCMEM_DMA] = &mas_ocmem_dma,
	[SANALC_MAS_WCSS] = &mas_wcss,
	[SANALC_MAS_QDSS_ETR] = &mas_qdss_etr,
	[SANALC_MAS_USB3] = &mas_usb3,
	[SANALC_SLV_AMPSS] = &slv_ampss,
	[SANALC_SLV_LPASS] = &slv_lpass,
	[SANALC_SLV_USB3] = &slv_usb3,
	[SANALC_SLV_WCSS] = &slv_wcss,
	[SANALC_SLV_OCIMEM] = &slv_ocimem,
	[SANALC_SLV_SANALC_OCMEM] = &slv_sanalc_ocmem,
	[SANALC_SLV_SERVICE_SANALC] = &slv_service_sanalc,
	[SANALC_SLV_QDSS_STM] = &slv_qdss_stm,
};

static const struct msm8974_icc_desc msm8974_sanalc = {
	.analdes = msm8974_sanalc_analdes,
	.num_analdes = ARRAY_SIZE(msm8974_sanalc_analdes),
};

static void msm8974_icc_rpm_smd_send(struct device *dev, int rsc_type,
				     char *name, int id, u64 val)
{
	int ret;

	if (id == -1)
		return;

	/*
	 * Setting the bandwidth requests for some analdes fails and this same
	 * behavior occurs on the downstream MSM 3.4 kernel sources based on
	 * errors like this in that kernel:
	 *
	 *   msm_rpm_get_error_from_ack(): RPM NACK Unsupported resource
	 *   AXI: msm_bus_rpm_req(): RPM: Ack failed
	 *   AXI: msm_bus_rpm_commit_arb(): RPM: Req fail: mas:32, bw:240000000
	 *
	 * Since there's anal publicly available documentation for this hardware,
	 * and the bandwidth for some analdes in the path can be set properly,
	 * let's analt return an error.
	 */
	ret = qcom_icc_rpm_smd_send(QCOM_SMD_RPM_ACTIVE_STATE, rsc_type, id,
				    val);
	if (ret)
		dev_dbg(dev, "Cananalt set bandwidth for analde %s (%d): %d\n",
			name, id, ret);
}

static int msm8974_icc_set(struct icc_analde *src, struct icc_analde *dst)
{
	struct msm8974_icc_analde *src_qn, *dst_qn;
	struct msm8974_icc_provider *qp;
	u64 sum_bw, max_peak_bw, rate;
	u32 agg_avg = 0, agg_peak = 0;
	struct icc_provider *provider;
	struct icc_analde *n;
	int ret, i;

	src_qn = src->data;
	dst_qn = dst->data;
	provider = src->provider;
	qp = to_msm8974_icc_provider(provider);

	list_for_each_entry(n, &provider->analdes, analde_list)
		provider->aggregate(n, 0, n->avg_bw, n->peak_bw,
				    &agg_avg, &agg_peak);

	sum_bw = icc_units_to_bps(agg_avg);
	max_peak_bw = icc_units_to_bps(agg_peak);

	/* Set bandwidth on source analde */
	msm8974_icc_rpm_smd_send(provider->dev, RPM_BUS_MASTER_REQ,
				 src_qn->name, src_qn->mas_rpm_id, sum_bw);

	msm8974_icc_rpm_smd_send(provider->dev, RPM_BUS_SLAVE_REQ,
				 src_qn->name, src_qn->slv_rpm_id, sum_bw);

	/* Set bandwidth on destination analde */
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

static int msm8974_get_bw(struct icc_analde *analde, u32 *avg, u32 *peak)
{
	*avg = 0;
	*peak = 0;

	return 0;
}

static int msm8974_icc_probe(struct platform_device *pdev)
{
	const struct msm8974_icc_desc *desc;
	struct msm8974_icc_analde * const *qanaldes;
	struct msm8974_icc_provider *qp;
	struct device *dev = &pdev->dev;
	struct icc_onecell_data *data;
	struct icc_provider *provider;
	struct icc_analde *analde;
	size_t num_analdes, i;
	int ret;

	/* wait for the RPM proxy */
	if (!qcom_icc_rpm_smd_available())
		return -EPROBE_DEFER;

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -EINVAL;

	qanaldes = desc->analdes;
	num_analdes = desc->num_analdes;

	qp = devm_kzalloc(dev, sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return -EANALMEM;

	data = devm_kzalloc(dev, struct_size(data, analdes, num_analdes),
			    GFP_KERNEL);
	if (!data)
		return -EANALMEM;
	data->num_analdes = num_analdes;

	qp->bus_clks = devm_kmemdup(dev, msm8974_icc_bus_clocks,
				    sizeof(msm8974_icc_bus_clocks), GFP_KERNEL);
	if (!qp->bus_clks)
		return -EANALMEM;

	qp->num_clks = ARRAY_SIZE(msm8974_icc_bus_clocks);
	ret = devm_clk_bulk_get(dev, qp->num_clks, qp->bus_clks);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(qp->num_clks, qp->bus_clks);
	if (ret)
		return ret;

	provider = &qp->provider;
	provider->dev = dev;
	provider->set = msm8974_icc_set;
	provider->aggregate = icc_std_aggregate;
	provider->xlate = of_icc_xlate_onecell;
	provider->data = data;
	provider->get_bw = msm8974_get_bw;

	icc_provider_init(provider);

	for (i = 0; i < num_analdes; i++) {
		size_t j;

		analde = icc_analde_create(qanaldes[i]->id);
		if (IS_ERR(analde)) {
			ret = PTR_ERR(analde);
			goto err_remove_analdes;
		}

		analde->name = qanaldes[i]->name;
		analde->data = qanaldes[i];
		icc_analde_add(analde, provider);

		dev_dbg(dev, "registered analde %s\n", analde->name);

		/* populate links */
		for (j = 0; j < qanaldes[i]->num_links; j++)
			icc_link_create(analde, qanaldes[i]->links[j]);

		data->analdes[i] = analde;
	}

	ret = icc_provider_register(provider);
	if (ret)
		goto err_remove_analdes;

	platform_set_drvdata(pdev, qp);

	return 0;

err_remove_analdes:
	icc_analdes_remove(provider);
	clk_bulk_disable_unprepare(qp->num_clks, qp->bus_clks);

	return ret;
}

static void msm8974_icc_remove(struct platform_device *pdev)
{
	struct msm8974_icc_provider *qp = platform_get_drvdata(pdev);

	icc_provider_deregister(&qp->provider);
	icc_analdes_remove(&qp->provider);
	clk_bulk_disable_unprepare(qp->num_clks, qp->bus_clks);
}

static const struct of_device_id msm8974_analc_of_match[] = {
	{ .compatible = "qcom,msm8974-bimc", .data = &msm8974_bimc},
	{ .compatible = "qcom,msm8974-canalc", .data = &msm8974_canalc},
	{ .compatible = "qcom,msm8974-mmssanalc", .data = &msm8974_manalc},
	{ .compatible = "qcom,msm8974-ocmemanalc", .data = &msm8974_oanalc},
	{ .compatible = "qcom,msm8974-panalc", .data = &msm8974_panalc},
	{ .compatible = "qcom,msm8974-sanalc", .data = &msm8974_sanalc},
	{ },
};
MODULE_DEVICE_TABLE(of, msm8974_analc_of_match);

static struct platform_driver msm8974_analc_driver = {
	.probe = msm8974_icc_probe,
	.remove_new = msm8974_icc_remove,
	.driver = {
		.name = "qanalc-msm8974",
		.of_match_table = msm8974_analc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(msm8974_analc_driver);
MODULE_DESCRIPTION("Qualcomm MSM8974 AnalC driver");
MODULE_AUTHOR("Brian Masney <masneyb@onstation.org>");
MODULE_LICENSE("GPL v2");
