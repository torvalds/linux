/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 MediaTek Inc.
 * Author: Qiqi Wang <qiqi.wang@mediatek.com>
 */

#ifndef __SOC_MEDIATEK_MT8189_PM_DOMAINS_H
#define __SOC_MEDIATEK_MT8189_PM_DOMAINS_H

#include "mtk-pm-domains.h"
#include <dt-bindings/power/mediatek,mt8189-power.h>

/*
 * MT8189 power domain support
 */

#define MT8189_SPM_PWR_STATUS				0x0f40
#define MT8189_SPM_PWR_STATUS_2ND			0x0f44
#define MT8189_SPM_PWR_STATUS_MSB			0x0f48
#define MT8189_SPM_PWR_STATUS_MSB_2ND			0x0f4c
#define MT8189_SPM_XPU_PWR_STATUS			0x0f50
#define MT8189_SPM_XPU_PWR_STATUS_2ND			0x0f54

#define MT8189_PROT_EN_EMICFG_GALS_SLP_SET		0x0084
#define MT8189_PROT_EN_EMICFG_GALS_SLP_CLR		0x0088
#define MT8189_PROT_EN_EMICFG_GALS_SLP_RDY		0x008c
#define MT8189_PROT_EN_MMSYS_STA_0_SET			0x0c14
#define MT8189_PROT_EN_MMSYS_STA_0_CLR			0x0c18
#define MT8189_PROT_EN_MMSYS_STA_0_RDY			0x0c1c
#define MT8189_PROT_EN_MMSYS_STA_1_SET			0x0c24
#define MT8189_PROT_EN_MMSYS_STA_1_CLR			0x0c28
#define MT8189_PROT_EN_MMSYS_STA_1_RDY			0x0c2c
#define MT8189_PROT_EN_INFRASYS_STA_0_SET		0x0c44
#define MT8189_PROT_EN_INFRASYS_STA_0_CLR		0x0c48
#define MT8189_PROT_EN_INFRASYS_STA_0_RDY		0x0c4c
#define MT8189_PROT_EN_INFRASYS_STA_1_SET		0x0c54
#define MT8189_PROT_EN_INFRASYS_STA_1_CLR		0x0c58
#define MT8189_PROT_EN_INFRASYS_STA_1_RDY		0x0c5c
#define MT8189_PROT_EN_PERISYS_STA_0_SET		0x0c84
#define MT8189_PROT_EN_PERISYS_STA_0_CLR		0x0c88
#define MT8189_PROT_EN_PERISYS_STA_0_RDY		0x0c8c
#define MT8189_PROT_EN_MCU_STA_0_SET			0x0c94
#define MT8189_PROT_EN_MCU_STA_0_CLR			0x0c98
#define MT8189_PROT_EN_MCU_STA_0_RDY			0x0c9c
#define MT8189_PROT_EN_MD_STA_0_SET			0x0ca4
#define MT8189_PROT_EN_MD_STA_0_CLR			0x0ca8
#define MT8189_PROT_EN_MD_STA_0_RDY			0x0cac

#define MT8189_PROT_EN_EMISYS_STA_0_MM_INFRA		(GENMASK(21, 20))
#define MT8189_PROT_EN_INFRASYS_STA_0_CONN		(BIT(8))
#define MT8189_PROT_EN_INFRASYS_STA_1_CONN		(BIT(12))
#define MT8189_PROT_EN_INFRASYS_STA_0_MM_INFRA		(BIT(16))
#define MT8189_PROT_EN_INFRASYS_STA_1_MM_INFRA		(BIT(11))
#define MT8189_PROT_EN_INFRASYS_STA_1_MFG1		(BIT(20))
#define MT8189_PROT_EN_MCU_STA_0_CONN			(BIT(1))
#define MT8189_PROT_EN_MCU_STA_0_CONN_2ND		(BIT(0))
#define MT8189_PROT_EN_MD_STA_0_MFG1			(BIT(0) | BIT(2))
#define MT8189_PROT_EN_MD_STA_0_MFG1_2ND		(BIT(4))
#define MT8189_PROT_EN_MM_INFRA_IGN			(BIT(1))
#define MT8189_PROT_EN_MM_INFRA_2_IGN			(BIT(0))
#define MT8189_PROT_EN_MMSYS_STA_0_CAM_MAIN		(GENMASK(31, 30))
#define MT8189_PROT_EN_MMSYS_STA_1_CAM_MAIN		(GENMASK(10, 9))
#define MT8189_PROT_EN_MMSYS_STA_0_DISP			(GENMASK(1, 0))
#define MT8189_PROT_EN_MMSYS_STA_0_ISP_IMG1		(BIT(3))
#define MT8189_PROT_EN_MMSYS_STA_1_ISP_IMG1		(BIT(7))
#define MT8189_PROT_EN_MMSYS_STA_0_ISP_IPE		(BIT(2))
#define MT8189_PROT_EN_MMSYS_STA_1_ISP_IPE		(BIT(8))
#define MT8189_PROT_EN_MMSYS_STA_0_MDP0			(BIT(18))
#define MT8189_PROT_EN_MMSYS_STA_1_MM_INFRA		(GENMASK(3, 2))
#define MT8189_PROT_EN_MMSYS_STA_1_MM_INFRA_2ND		(GENMASK(15, 7))
#define MT8189_PROT_EN_MMSYS_STA_0_VDE0			(BIT(20))
#define MT8189_PROT_EN_MMSYS_STA_1_VDE0			(BIT(13))
#define MT8189_PROT_EN_MMSYS_STA_0_VEN0			(BIT(12))
#define MT8189_PROT_EN_MMSYS_STA_1_VEN0			(BIT(12))
#define MT8189_PROT_EN_PERISYS_STA_0_AUDIO		(BIT(6))
#define MT8189_PROT_EN_PERISYS_STA_0_SSUSB		(BIT(7))
#define MT8189_PROT_EN_EMICFG_GALS_SLP_MFG1		(GENMASK(5, 4))

static enum scpsys_bus_prot_block scpsys_bus_prot_blocks_mt8189[] = {
	BUS_PROT_BLOCK_INFRA, BUS_PROT_BLOCK_SMI
};

static const struct scpsys_domain_data scpsys_domain_data_mt8189[] = {
	[MT8189_POWER_DOMAIN_CONN] = {
		.name = "conn",
		.sta_mask = BIT(1),
		.ctl_offs = 0xe04,
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_2ND,
		.bp_cfg = {
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_MCU_STA_0_CONN,
					MT8189_PROT_EN_MCU_STA_0_SET,
					MT8189_PROT_EN_MCU_STA_0_CLR,
					MT8189_PROT_EN_MCU_STA_0_RDY),
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_INFRASYS_STA_1_CONN,
					MT8189_PROT_EN_INFRASYS_STA_1_SET,
					MT8189_PROT_EN_INFRASYS_STA_1_CLR,
					MT8189_PROT_EN_INFRASYS_STA_1_RDY),
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_MCU_STA_0_CONN_2ND,
					MT8189_PROT_EN_MCU_STA_0_SET,
					MT8189_PROT_EN_MCU_STA_0_CLR,
					MT8189_PROT_EN_MCU_STA_0_RDY),
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_INFRASYS_STA_0_CONN,
					MT8189_PROT_EN_INFRASYS_STA_0_SET,
					MT8189_PROT_EN_INFRASYS_STA_0_CLR,
					MT8189_PROT_EN_INFRASYS_STA_0_RDY),
		},
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT8189_POWER_DOMAIN_AUDIO] = {
		.name = "audio",
		.sta_mask = BIT(6),
		.ctl_offs = 0xe18,
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_PERISYS_STA_0_AUDIO,
					MT8189_PROT_EN_PERISYS_STA_0_SET,
					MT8189_PROT_EN_PERISYS_STA_0_CLR,
					MT8189_PROT_EN_PERISYS_STA_0_RDY),
		},
	},
	[MT8189_POWER_DOMAIN_ADSP_TOP_DORMANT] = {
		.name = "adsp-top-dormant",
		.sta_mask = BIT(7),
		.ctl_offs = 0xe1c,
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = BIT(9),
		.sram_pdn_ack_bits = BIT(13),
		.caps = MTK_SCPD_SRAM_ISO | MTK_SCPD_SRAM_PDN_INVERTED |
			MTK_SCPD_ACTIVE_WAKEUP | MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT8189_POWER_DOMAIN_ADSP_INFRA] = {
		.name = "adsp-infra",
		.sta_mask = BIT(8),
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_2ND,
		.ctl_offs = 0xe20,
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT8189_POWER_DOMAIN_ADSP_AO] = {
		.name = "adsp-ao",
		.sta_mask = BIT(9),
		.ctl_offs = 0xe24,
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_2ND,
	},
	[MT8189_POWER_DOMAIN_ISP_IMG1] = {
		.name = "isp-img1",
		.sta_mask = BIT(10),
		.ctl_offs = 0xe28,
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_MMSYS_STA_0_ISP_IMG1,
					MT8189_PROT_EN_MMSYS_STA_0_SET,
					MT8189_PROT_EN_MMSYS_STA_0_CLR,
					MT8189_PROT_EN_MMSYS_STA_0_RDY),
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_MMSYS_STA_1_ISP_IMG1,
					MT8189_PROT_EN_MMSYS_STA_1_SET,
					MT8189_PROT_EN_MMSYS_STA_1_CLR,
					MT8189_PROT_EN_MMSYS_STA_1_RDY),
		},
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT8189_POWER_DOMAIN_ISP_IMG2] = {
		.name = "isp-img2",
		.sta_mask = BIT(11),
		.ctl_offs = 0xe2c,
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT8189_POWER_DOMAIN_ISP_IPE] = {
		.name = "isp-ipe",
		.sta_mask = BIT(12),
		.ctl_offs = 0xe30,
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_MMSYS_STA_0_ISP_IPE,
					MT8189_PROT_EN_MMSYS_STA_0_SET,
					MT8189_PROT_EN_MMSYS_STA_0_CLR,
					MT8189_PROT_EN_MMSYS_STA_0_RDY),
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_MMSYS_STA_1_ISP_IPE,
					MT8189_PROT_EN_MMSYS_STA_1_SET,
					MT8189_PROT_EN_MMSYS_STA_1_CLR,
					MT8189_PROT_EN_MMSYS_STA_1_RDY),
		},
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT8189_POWER_DOMAIN_VDE0] = {
		.name = "vde0",
		.sta_mask = BIT(14),
		.ctl_offs = 0xe38,
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_MMSYS_STA_0_VDE0,
					MT8189_PROT_EN_MMSYS_STA_0_SET,
					MT8189_PROT_EN_MMSYS_STA_0_CLR,
					MT8189_PROT_EN_MMSYS_STA_0_RDY),
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_MMSYS_STA_1_VDE0,
					MT8189_PROT_EN_MMSYS_STA_1_SET,
					MT8189_PROT_EN_MMSYS_STA_1_CLR,
					MT8189_PROT_EN_MMSYS_STA_1_RDY),
		},
	},
	[MT8189_POWER_DOMAIN_VEN0] = {
		.name = "ven0",
		.sta_mask = BIT(16),
		.ctl_offs = 0xe40,
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_MMSYS_STA_0_VEN0,
					MT8189_PROT_EN_MMSYS_STA_0_SET,
					MT8189_PROT_EN_MMSYS_STA_0_CLR,
					MT8189_PROT_EN_MMSYS_STA_0_RDY),
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_MMSYS_STA_1_VEN0,
					MT8189_PROT_EN_MMSYS_STA_1_SET,
					MT8189_PROT_EN_MMSYS_STA_1_CLR,
					MT8189_PROT_EN_MMSYS_STA_1_RDY),
		},
	},
	[MT8189_POWER_DOMAIN_CAM_MAIN] = {
		.name = "cam-main",
		.sta_mask = BIT(18),
		.ctl_offs = 0xe48,
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_MMSYS_STA_0_CAM_MAIN,
					MT8189_PROT_EN_MMSYS_STA_0_SET,
					MT8189_PROT_EN_MMSYS_STA_0_CLR,
					MT8189_PROT_EN_MMSYS_STA_0_RDY),
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_MMSYS_STA_1_CAM_MAIN,
					MT8189_PROT_EN_MMSYS_STA_1_SET,
					MT8189_PROT_EN_MMSYS_STA_1_CLR,
					MT8189_PROT_EN_MMSYS_STA_1_RDY),
		},
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT8189_POWER_DOMAIN_CAM_SUBA] = {
		.name = "cam-suba",
		.sta_mask = BIT(20),
		.ctl_offs = 0xe50,
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT8189_POWER_DOMAIN_CAM_SUBB] = {
		.name = "cam-subb",
		.sta_mask = BIT(21),
		.ctl_offs = 0xe54,
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT8189_POWER_DOMAIN_MDP0] = {
		.name = "mdp0",
		.sta_mask = BIT(26),
		.ctl_offs = 0xe68,
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_MMSYS_STA_0_MDP0,
					MT8189_PROT_EN_MMSYS_STA_0_SET,
					MT8189_PROT_EN_MMSYS_STA_0_CLR,
					MT8189_PROT_EN_MMSYS_STA_0_RDY),
		},
	},
	[MT8189_POWER_DOMAIN_DISP] = {
		.name = "disp",
		.sta_mask = BIT(28),
		.ctl_offs = 0xe70,
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_MMSYS_STA_0_DISP,
					MT8189_PROT_EN_MMSYS_STA_0_SET,
					MT8189_PROT_EN_MMSYS_STA_0_CLR,
					MT8189_PROT_EN_MMSYS_STA_0_RDY),
		},
	},
	[MT8189_POWER_DOMAIN_MM_INFRA] = {
		.name = "mm-infra",
		.sta_mask = BIT(30),
		.ctl_offs = 0xe78,
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_MMSYS_STA_1_MM_INFRA,
					MT8189_PROT_EN_MMSYS_STA_1_SET,
					MT8189_PROT_EN_MMSYS_STA_1_CLR,
					MT8189_PROT_EN_MMSYS_STA_1_RDY),
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_MMSYS_STA_1_MM_INFRA_2ND,
					MT8189_PROT_EN_MMSYS_STA_1_SET,
					MT8189_PROT_EN_MMSYS_STA_1_CLR,
					MT8189_PROT_EN_MMSYS_STA_1_RDY),
			BUS_PROT_WR_IGN_SUBCLK(INFRA,
					       MT8189_PROT_EN_MM_INFRA_IGN,
					       MT8189_PROT_EN_MMSYS_STA_1_SET,
					       MT8189_PROT_EN_MMSYS_STA_1_CLR,
					       MT8189_PROT_EN_MMSYS_STA_1_RDY),
			BUS_PROT_WR_IGN_SUBCLK(INFRA,
					       MT8189_PROT_EN_MM_INFRA_2_IGN,
					       MT8189_PROT_EN_MMSYS_STA_1_SET,
					       MT8189_PROT_EN_MMSYS_STA_1_CLR,
					       MT8189_PROT_EN_MMSYS_STA_1_RDY),
		},
	},
	[MT8189_POWER_DOMAIN_DP_TX] = {
		.name = "dp-tx",
		.sta_mask = BIT(0),
		.ctl_offs = 0xe80,
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS_MSB,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_MSB_2ND,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
	},
	[MT8189_POWER_DOMAIN_CSI_RX] = {
		.name = "csi-rx",
		.sta_mask = BIT(7),
		.ctl_offs = 0xe9c,
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS_MSB,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_MSB_2ND,
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT8189_POWER_DOMAIN_SSUSB] = {
		.name = "ssusb",
		.sta_mask = BIT(10),
		.ctl_offs = 0xea8,
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS_MSB,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_MSB_2ND,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_PERISYS_STA_0_SSUSB,
					MT8189_PROT_EN_PERISYS_STA_0_SET,
					MT8189_PROT_EN_PERISYS_STA_0_CLR,
					MT8189_PROT_EN_PERISYS_STA_0_RDY),
		},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT8189_POWER_DOMAIN_MFG0] = {
		.name = "mfg0",
		.sta_mask = BIT(1),
		.ctl_offs = 0xeb4,
		.pwr_sta_offs = MT8189_SPM_XPU_PWR_STATUS,
		.pwr_sta2nd_offs = MT8189_SPM_XPU_PWR_STATUS_2ND,
		.caps = MTK_SCPD_DOMAIN_SUPPLY,
	},
	[MT8189_POWER_DOMAIN_MFG1] = {
		.name = "mfg1",
		.sta_mask = BIT(2),
		.ctl_offs = 0xeb8,
		.pwr_sta_offs = MT8189_SPM_XPU_PWR_STATUS,
		.pwr_sta2nd_offs = MT8189_SPM_XPU_PWR_STATUS_2ND,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_INFRASYS_STA_1_MFG1,
					MT8189_PROT_EN_INFRASYS_STA_1_SET,
					MT8189_PROT_EN_INFRASYS_STA_1_CLR,
					MT8189_PROT_EN_INFRASYS_STA_1_RDY),
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_MD_STA_0_MFG1,
					MT8189_PROT_EN_MD_STA_0_SET,
					MT8189_PROT_EN_MD_STA_0_CLR,
					MT8189_PROT_EN_MD_STA_0_RDY),
			BUS_PROT_WR_IGN(INFRA,
					MT8189_PROT_EN_MD_STA_0_MFG1_2ND,
					MT8189_PROT_EN_MD_STA_0_SET,
					MT8189_PROT_EN_MD_STA_0_CLR,
					MT8189_PROT_EN_MD_STA_0_RDY),
			BUS_PROT_WR_IGN(SMI,
					MT8189_PROT_EN_EMICFG_GALS_SLP_MFG1,
					MT8189_PROT_EN_EMICFG_GALS_SLP_SET,
					MT8189_PROT_EN_EMICFG_GALS_SLP_CLR,
					MT8189_PROT_EN_EMICFG_GALS_SLP_RDY),
		},
		.caps = MTK_SCPD_DOMAIN_SUPPLY,
	},
	[MT8189_POWER_DOMAIN_MFG2] = {
		.name = "mfg2",
		.sta_mask = BIT(3),
		.ctl_offs = 0xebc,
		.pwr_sta_offs = MT8189_SPM_XPU_PWR_STATUS,
		.pwr_sta2nd_offs = MT8189_SPM_XPU_PWR_STATUS_2ND,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
	},
	[MT8189_POWER_DOMAIN_MFG3] = {
		.name = "mfg3",
		.sta_mask = BIT(4),
		.ctl_offs = 0xec0,
		.pwr_sta_offs = MT8189_SPM_XPU_PWR_STATUS,
		.pwr_sta2nd_offs = MT8189_SPM_XPU_PWR_STATUS_2ND,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
	},
	[MT8189_POWER_DOMAIN_EDP_TX_DORMANT] = {
		.name = "edp-tx-dormant",
		.sta_mask = BIT(12),
		.ctl_offs = 0xf70,
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS_MSB,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_MSB_2ND,
		.sram_pdn_bits = BIT(9),
		.sram_pdn_ack_bits = 0,
		.caps = MTK_SCPD_SRAM_ISO | MTK_SCPD_SRAM_PDN_INVERTED,
	},
	[MT8189_POWER_DOMAIN_PCIE] = {
		.name = "pcie",
		.sta_mask = BIT(13),
		.ctl_offs = 0xf74,
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS_MSB,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_MSB_2ND,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT8189_POWER_DOMAIN_PCIE_PHY] = {
		.name = "pcie-phy",
		.sta_mask = BIT(14),
		.ctl_offs = 0xf78,
		.pwr_sta_offs = MT8189_SPM_PWR_STATUS_MSB,
		.pwr_sta2nd_offs = MT8189_SPM_PWR_STATUS_MSB_2ND,
	},
};

static const struct scpsys_soc_data mt8189_scpsys_data = {
	.domains_data = scpsys_domain_data_mt8189,
	.num_domains = ARRAY_SIZE(scpsys_domain_data_mt8189),
	.bus_prot_blocks = scpsys_bus_prot_blocks_mt8189,
	.num_bus_prot_blocks = ARRAY_SIZE(scpsys_bus_prot_blocks_mt8189),
};

#endif /* __SOC_MEDIATEK_MT8189_PM_DOMAINS_H */
