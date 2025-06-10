/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Collabora Ltd
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#ifndef __PMDOMAIN_MEDIATEK_MT6893_PM_DOMAINS_H
#define __PMDOMAIN_MEDIATEK_MT6893_PM_DOMAINS_H

#include <linux/soc/mediatek/infracfg.h>
#include <dt-bindings/power/mediatek,mt6893-power.h>
#include "mtk-pm-domains.h"

#define MT6893_TOP_AXI_PROT_EN_MCU_STA1				0x2e4
#define MT6893_TOP_AXI_PROT_EN_MCU_SET				0x2c4
#define MT6893_TOP_AXI_PROT_EN_MCU_CLR				0x2c8
#define MT6893_TOP_AXI_PROT_EN_VDNR_1_SET			0xba4
#define MT6893_TOP_AXI_PROT_EN_VDNR_1_CLR			0xba8
#define MT6893_TOP_AXI_PROT_EN_VDNR_1_STA1			0xbb0
#define MT6893_TOP_AXI_PROT_EN_SUB_INFRA_VDNR_SET		0xbb8
#define MT6893_TOP_AXI_PROT_EN_SUB_INFRA_VDNR_CLR		0xbbc
#define MT6893_TOP_AXI_PROT_EN_SUB_INFRA_VDNR_STA1		0xbc4

#define MT6893_TOP_AXI_PROT_EN_1_MFG1_STEP1			GENMASK(21, 19)
#define MT6893_TOP_AXI_PROT_EN_2_MFG1_STEP2			GENMASK(6, 5)
#define MT6893_TOP_AXI_PROT_EN_MFG1_STEP3			GENMASK(22, 21)
#define MT6893_TOP_AXI_PROT_EN_2_MFG1_STEP4			BIT(7)
#define MT6893_TOP_AXI_PROT_EN_SUB_INFRA_VDNR_MFG1_STEP5	GENMASK(19, 17)
#define MT6893_TOP_AXI_PROT_EN_MM_VDEC0_STEP1			BIT(24)
#define MT6893_TOP_AXI_PROT_EN_MM_2_VDEC0_STEP2			BIT(25)
#define MT6893_TOP_AXI_PROT_EN_MM_VDEC1_STEP1			BIT(6)
#define MT6893_TOP_AXI_PROT_EN_MM_VDEC1_STEP2			BIT(7)
#define MT6893_TOP_AXI_PROT_EN_MM_VENC0_STEP1			BIT(26)
#define MT6893_TOP_AXI_PROT_EN_MM_2_VENC0_STEP2			BIT(0)
#define MT6893_TOP_AXI_PROT_EN_MM_VENC0_STEP3			BIT(27)
#define MT6893_TOP_AXI_PROT_EN_MM_2_VENC0_STEP4			BIT(1)
#define MT6893_TOP_AXI_PROT_EN_MM_VENC1_STEP1			GENMASK(30, 28)
#define MT6893_TOP_AXI_PROT_EN_MM_VENC1_STEP2			GENMASK(31, 29)
#define MT6893_TOP_AXI_PROT_EN_MDP_STEP1			BIT(10)
#define MT6893_TOP_AXI_PROT_EN_MM_MDP_STEP2			(BIT(2) | BIT(4) | BIT(6) |	\
								 BIT(8) | BIT(18) | BIT(22) |	\
								 BIT(28) | BIT(30))
#define MT6893_TOP_AXI_PROT_EN_MM_2_MDP_STEP3			(BIT(0) | BIT(2) | BIT(4) |	\
								 BIT(6) | BIT(8))
#define MT6893_TOP_AXI_PROT_EN_MDP_STEP4			BIT(23)
#define MT6893_TOP_AXI_PROT_EN_MM_MDP_STEP5			(BIT(3) | BIT(5) | BIT(7) |	\
								 BIT(9) | BIT(19) | BIT(23) |	\
								 BIT(29) | BIT(31))
#define MT6893_TOP_AXI_PROT_EN_MM_2_MDP_STEP6			(BIT(1) | BIT(7) | BIT(9) | BIT(11))
#define MT6893_TOP_AXI_PROT_EN_SUB_INFRA_VDNR_MDP_STEP7		BIT(20)
#define MT6893_TOP_AXI_PROT_EN_MM_DISP_STEP1			(BIT(0) | BIT(6) | BIT(8) |	\
								 BIT(10) | BIT(12) | BIT(14) |	\
								 BIT(16) | BIT(20) | BIT(24) |	\
								 BIT(26))
#define MT6893_TOP_AXI_PROT_EN_DISP_STEP2			BIT(6)
#define MT6893_TOP_AXI_PROT_EN_MM_DISP_STEP3			(BIT(1) | BIT(7) | BIT(9) |	\
								 BIT(15) | BIT(17) | BIT(21) |	\
								 BIT(25) | BIT(27))
#define MT6893_TOP_AXI_PROT_EN_SUB_INFRA_VDNR_DISP_STEP4	BIT(21)
#define MT6893_TOP_AXI_PROT_EN_2_ADSP				BIT(3)
#define MT6893_TOP_AXI_PROT_EN_2_CAM_STEP1			BIT(1)
#define MT6893_TOP_AXI_PROT_EN_MM_CAM_STEP2			(BIT(0) | BIT(2) | BIT(4))
#define MT6893_TOP_AXI_PROT_EN_1_CAM_STEP3			BIT(22)
#define MT6893_TOP_AXI_PROT_EN_MM_CAM_STEP4			(BIT(1) | BIT(3) | BIT(5))
#define MT6893_TOP_AXI_PROT_EN_MM_CAM_RAWA_STEP1		BIT(18)
#define MT6893_TOP_AXI_PROT_EN_MM_CAM_RAWA_STEP2		BIT(19)
#define MT6893_TOP_AXI_PROT_EN_MM_CAM_RAWB_STEP1		BIT(20)
#define MT6893_TOP_AXI_PROT_EN_MM_CAM_RAWB_STEP2		BIT(21)
#define MT6893_TOP_AXI_PROT_EN_MM_CAM_RAWC_STEP1		BIT(22)
#define MT6893_TOP_AXI_PROT_EN_MM_CAM_RAWC_STEP2		BIT(23)

/*
 * MT6893 Power Domain (MTCMOS) support
 *
 * The register layout for this IP is very similar to MT8192 so where possible
 * the same definitions are reused to avoid duplication.
 * Where the bus protection bits are also the same, the entire set is reused.
 */
static const struct scpsys_domain_data scpsys_domain_data_mt6893[] = {
	[MT6893_POWER_DOMAIN_CONN] = {
		.name = "conn",
		.sta_mask = BIT(1),
		.ctl_offs = 0x304,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = 0,
		.sram_pdn_ack_bits = 0,
		.bp_cfg = {
			BUS_PROT_WR(INFRA,
				    MT8192_TOP_AXI_PROT_EN_CONN,
				    MT8192_TOP_AXI_PROT_EN_SET,
				    MT8192_TOP_AXI_PROT_EN_CLR,
				    MT8192_TOP_AXI_PROT_EN_STA1),
			BUS_PROT_WR(INFRA,
				    MT8192_TOP_AXI_PROT_EN_CONN_2ND,
				    MT8192_TOP_AXI_PROT_EN_SET,
				    MT8192_TOP_AXI_PROT_EN_CLR,
				    MT8192_TOP_AXI_PROT_EN_STA1),
			BUS_PROT_WR(INFRA,
				    MT8192_TOP_AXI_PROT_EN_1_CONN,
				    MT8192_TOP_AXI_PROT_EN_1_SET,
				    MT8192_TOP_AXI_PROT_EN_1_CLR,
				    MT8192_TOP_AXI_PROT_EN_1_STA1),
		},
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT6893_POWER_DOMAIN_MFG0] = {
		.name = "mfg0",
		.sta_mask = BIT(2),
		.ctl_offs = 0x308,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF | MTK_SCPD_DOMAIN_SUPPLY,
	},
	[MT6893_POWER_DOMAIN_MFG1] = {
		.name = "mfg1",
		.sta_mask = BIT(3),
		.ctl_offs = 0x30c,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_1_MFG1_STEP1,
				    MT8192_TOP_AXI_PROT_EN_1_SET,
				    MT8192_TOP_AXI_PROT_EN_1_CLR,
				    MT8192_TOP_AXI_PROT_EN_1_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_2_MFG1_STEP2,
				    MT8192_TOP_AXI_PROT_EN_2_SET,
				    MT8192_TOP_AXI_PROT_EN_2_CLR,
				    MT8192_TOP_AXI_PROT_EN_2_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MFG1_STEP3,
				    MT8192_TOP_AXI_PROT_EN_SET,
				    MT8192_TOP_AXI_PROT_EN_CLR,
				    MT8192_TOP_AXI_PROT_EN_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_2_MFG1_STEP4,
				    MT8192_TOP_AXI_PROT_EN_2_SET,
				    MT8192_TOP_AXI_PROT_EN_2_CLR,
				    MT8192_TOP_AXI_PROT_EN_2_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_SUB_INFRA_VDNR_MFG1_STEP5,
				    MT6893_TOP_AXI_PROT_EN_SUB_INFRA_VDNR_SET,
				    MT6893_TOP_AXI_PROT_EN_SUB_INFRA_VDNR_CLR,
				    MT6893_TOP_AXI_PROT_EN_SUB_INFRA_VDNR_STA1),
		},
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF | MTK_SCPD_DOMAIN_SUPPLY,
	},
	[MT6893_POWER_DOMAIN_MFG2] = {
		.name = "mfg2",
		.sta_mask = BIT(4),
		.ctl_offs = 0x310,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT6893_POWER_DOMAIN_MFG3] = {
		.name = "mfg3",
		.sta_mask = BIT(5),
		.ctl_offs = 0x314,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT6893_POWER_DOMAIN_MFG4] = {
		.name = "mfg4",
		.sta_mask = BIT(6),
		.ctl_offs = 0x318,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT6893_POWER_DOMAIN_MFG5] = {
		.name = "mfg5",
		.sta_mask = BIT(7),
		.ctl_offs = 0x31c,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT6893_POWER_DOMAIN_MFG6] = {
		.name = "mfg6",
		.sta_mask = BIT(8),
		.ctl_offs = 0x320,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT6893_POWER_DOMAIN_ISP] = {
		.name = "isp",
		.sta_mask = BIT(12),
		.ctl_offs = 0x330,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR(INFRA,
				    MT8192_TOP_AXI_PROT_EN_MM_2_ISP,
				    MT8192_TOP_AXI_PROT_EN_MM_2_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_2_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_2_STA1),
			BUS_PROT_WR(INFRA,
				    MT8192_TOP_AXI_PROT_EN_MM_2_ISP_2ND,
				    MT8192_TOP_AXI_PROT_EN_MM_2_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_2_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_2_STA1),
		},
	},
	[MT6893_POWER_DOMAIN_ISP2] = {
		.name = "isp2",
		.sta_mask = BIT(13),
		.ctl_offs = 0x334,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR(INFRA,
				    MT8192_TOP_AXI_PROT_EN_MM_ISP2,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
			BUS_PROT_WR(INFRA,
				    MT8192_TOP_AXI_PROT_EN_MM_ISP2_2ND,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
		},
	},
	[MT6893_POWER_DOMAIN_IPE] = {
		.name = "ipe",
		.sta_mask = BIT(14),
		.ctl_offs = 0x338,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR(INFRA,
				    MT8192_TOP_AXI_PROT_EN_MM_IPE,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
			BUS_PROT_WR(INFRA,
				    MT8192_TOP_AXI_PROT_EN_MM_IPE_2ND,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
		},
	},
	[MT6893_POWER_DOMAIN_VDEC0] = {
		.name = "vdec0",
		.sta_mask = BIT(15),
		.ctl_offs = 0x33c,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_VDEC0_STEP1,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_2_VDEC0_STEP2,
				    MT8192_TOP_AXI_PROT_EN_MM_2_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_2_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
		},
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT6893_POWER_DOMAIN_VDEC1] = {
		.name = "vdec1",
		.sta_mask = BIT(16),
		.ctl_offs = 0x340,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_VDEC1_STEP1,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_VDEC1_STEP2,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
		},
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT6893_POWER_DOMAIN_VENC0] = {
		.name = "venc0",
		.sta_mask = BIT(17),
		.ctl_offs = 0x344,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_VENC0_STEP1,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_2_VENC0_STEP2,
				    MT8192_TOP_AXI_PROT_EN_MM_2_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_2_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_2_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_VENC0_STEP3,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_2_VENC0_STEP4,
				    MT8192_TOP_AXI_PROT_EN_MM_2_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_2_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_2_STA1),
		},
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT6893_POWER_DOMAIN_VENC1] = {
		.name = "venc1",
		.sta_mask = BIT(18),
		.ctl_offs = 0x348,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_VENC1_STEP1,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_VENC1_STEP2,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
		},
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT6893_POWER_DOMAIN_MDP] = {
		.name = "mdp",
		.sta_mask = BIT(19),
		.ctl_offs = 0x34c,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MDP_STEP1,
				    MT8192_TOP_AXI_PROT_EN_SET,
				    MT8192_TOP_AXI_PROT_EN_CLR,
				    MT8192_TOP_AXI_PROT_EN_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_MDP_STEP2,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_2_MDP_STEP3,
				    MT8192_TOP_AXI_PROT_EN_MM_2_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_2_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_2_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MDP_STEP4,
				    MT8192_TOP_AXI_PROT_EN_SET,
				    MT8192_TOP_AXI_PROT_EN_CLR,
				    MT8192_TOP_AXI_PROT_EN_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_MDP_STEP5,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_2_MDP_STEP6,
				    MT8192_TOP_AXI_PROT_EN_MM_2_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_2_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_2_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_SUB_INFRA_VDNR_MDP_STEP7,
				    MT6893_TOP_AXI_PROT_EN_SUB_INFRA_VDNR_SET,
				    MT6893_TOP_AXI_PROT_EN_SUB_INFRA_VDNR_CLR,
				    MT6893_TOP_AXI_PROT_EN_SUB_INFRA_VDNR_STA1),
		},
	},
	[MT6893_POWER_DOMAIN_DISP] = {
		.name = "disp",
		.sta_mask = BIT(20),
		.ctl_offs = 0x350,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_DISP_STEP1,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_DISP_STEP2,
				    MT8192_TOP_AXI_PROT_EN_SET,
				    MT8192_TOP_AXI_PROT_EN_CLR,
				    MT8192_TOP_AXI_PROT_EN_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_DISP_STEP3,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_SUB_INFRA_VDNR_DISP_STEP4,
				    MT6893_TOP_AXI_PROT_EN_SUB_INFRA_VDNR_SET,
				    MT6893_TOP_AXI_PROT_EN_SUB_INFRA_VDNR_CLR,
				    MT6893_TOP_AXI_PROT_EN_SUB_INFRA_VDNR_STA1),
		},
	},
	[MT6893_POWER_DOMAIN_AUDIO] = {
		.name = "audio",
		.sta_mask = BIT(21),
		.ctl_offs = 0x354,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR(INFRA,
				    MT8192_TOP_AXI_PROT_EN_2_AUDIO,
				    MT8192_TOP_AXI_PROT_EN_2_SET,
				    MT8192_TOP_AXI_PROT_EN_2_CLR,
				    MT8192_TOP_AXI_PROT_EN_2_STA1),
		},
	},
	[MT6893_POWER_DOMAIN_ADSP] = {
		.name = "audio",
		.sta_mask = BIT(22),
		.ctl_offs = 0x358,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(9),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_2_ADSP,
				    MT8192_TOP_AXI_PROT_EN_2_SET,
				    MT8192_TOP_AXI_PROT_EN_2_CLR,
				    MT8192_TOP_AXI_PROT_EN_2_STA1),
		},
	},
	[MT6893_POWER_DOMAIN_CAM] = {
		.name = "cam",
		.sta_mask = BIT(23),
		.ctl_offs = 0x35c,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_2_CAM_STEP1,
				    MT8192_TOP_AXI_PROT_EN_2_SET,
				    MT8192_TOP_AXI_PROT_EN_2_CLR,
				    MT8192_TOP_AXI_PROT_EN_2_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_CAM_STEP2,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_1_CAM_STEP3,
				    MT8192_TOP_AXI_PROT_EN_1_SET,
				    MT8192_TOP_AXI_PROT_EN_1_CLR,
				    MT8192_TOP_AXI_PROT_EN_1_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_CAM_STEP4,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
		},
	},
	[MT6893_POWER_DOMAIN_CAM_RAWA] = {
		.name = "cam_rawa",
		.sta_mask = BIT(24),
		.ctl_offs = 0x360,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_CAM_RAWA_STEP1,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_CAM_RAWA_STEP2,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
		},
	},
	[MT6893_POWER_DOMAIN_CAM_RAWB] = {
		.name = "cam_rawb",
		.sta_mask = BIT(25),
		.ctl_offs = 0x364,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_CAM_RAWB_STEP1,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_CAM_RAWB_STEP2,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
		},
	},
	[MT6893_POWER_DOMAIN_CAM_RAWC] = {
		.name = "cam_rawc",
		.sta_mask = BIT(26),
		.ctl_offs = 0x368,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_CAM_RAWC_STEP1,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
			BUS_PROT_WR(INFRA,
				    MT6893_TOP_AXI_PROT_EN_MM_CAM_RAWC_STEP2,
				    MT8192_TOP_AXI_PROT_EN_MM_SET,
				    MT8192_TOP_AXI_PROT_EN_MM_CLR,
				    MT8192_TOP_AXI_PROT_EN_MM_STA1),
		},
	},
	[MT6893_POWER_DOMAIN_DP_TX] = {
		.name = "dp_tx",
		.sta_mask = BIT(27),
		.ctl_offs = 0x3ac,
		.pwr_sta_offs = 0x16c,
		.pwr_sta2nd_offs = 0x170,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
};

static const struct scpsys_soc_data mt6893_scpsys_data = {
	.domains_data = scpsys_domain_data_mt6893,
	.num_domains = ARRAY_SIZE(scpsys_domain_data_mt6893),
};

#endif /* __PMDOMAIN_MEDIATEK_MT6893_PM_DOMAINS_H */
