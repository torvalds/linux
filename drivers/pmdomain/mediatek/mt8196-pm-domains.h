/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Collabora Ltd
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#ifndef __SOC_MEDIATEK_MT8196_PM_DOMAINS_H
#define __SOC_MEDIATEK_MT8196_PM_DOMAINS_H

#include "mtk-pm-domains.h"
#include <dt-bindings/power/mediatek,mt8196-power.h>

/*
 * MT8196 and MT6991 power domain support
 */

/* INFRA TOP_AXI registers */
#define MT8196_TOP_AXI_PROT_EN_SET		0x4
#define MT8196_TOP_AXI_PROT_EN_CLR		0x8
#define MT8196_TOP_AXI_PROT_EN_STA		0xc
 #define MT8196_TOP_AXI_PROT_EN_SLEEP0_MD	BIT(29)

#define MT8196_TOP_AXI_PROT_EN_1_SET		0x24
#define MT8196_TOP_AXI_PROT_EN_1_CLR		0x28
#define MT8196_TOP_AXI_PROT_EN_1_STA		0x2c
 #define MT8196_TOP_AXI_PROT_EN_1_SLEEP1_MD	BIT(0)

/* SPM BUS_PROTECT registers */
#define MT8196_SPM_BUS_PROTECT_CON_SET		0xdc
#define MT8196_SPM_BUS_PROTECT_CON_CLR		0xe0
#define MT8196_SPM_BUS_PROTECT_RDY		0x208
 #define MT8196_SPM_PROT_EN_BUS_CONN		BIT(1)
 #define MT8196_SPM_PROT_EN_BUS_SSUSB_DP_PHY_P0	BIT(6)
 #define MT8196_SPM_PROT_EN_BUS_SSUSB_P0	BIT(7)
 #define MT8196_SPM_PROT_EN_BUS_SSUSB_P1	BIT(8)
 #define MT8196_SPM_PROT_EN_BUS_SSUSB_P23	BIT(9)
 #define MT8196_SPM_PROT_EN_BUS_SSUSB_PHY_P2	BIT(10)
 #define MT8196_SPM_PROT_EN_BUS_PEXTP_MAC0	BIT(13)
 #define MT8196_SPM_PROT_EN_BUS_PEXTP_MAC1	BIT(14)
 #define MT8196_SPM_PROT_EN_BUS_PEXTP_MAC2	BIT(15)
 #define MT8196_SPM_PROT_EN_BUS_PEXTP_PHY0	BIT(16)
 #define MT8196_SPM_PROT_EN_BUS_PEXTP_PHY1	BIT(17)
 #define MT8196_SPM_PROT_EN_BUS_PEXTP_PHY2	BIT(18)
 #define MT8196_SPM_PROT_EN_BUS_AUDIO		BIT(19)
 #define MT8196_SPM_PROT_EN_BUS_ADSP_TOP	BIT(21)
 #define MT8196_SPM_PROT_EN_BUS_ADSP_INFRA	BIT(22)
 #define MT8196_SPM_PROT_EN_BUS_ADSP_AO		BIT(23)
 #define MT8196_SPM_PROT_EN_BUS_MM_PROC		BIT(24)

/* PWR_CON registers */
#define MT8196_PWR_ACK				BIT(30)
#define MT8196_PWR_ACK_2ND			BIT(31)

static enum scpsys_bus_prot_block scpsys_bus_prot_blocks_mt8196[] = {
	BUS_PROT_BLOCK_INFRA, BUS_PROT_BLOCK_SPM
};

static const struct scpsys_domain_data scpsys_domain_data_mt8196[] = {
	[MT8196_POWER_DOMAIN_MD] = {
		.name = "md",
		.sta_mask = MT8196_PWR_ACK,
		.sta2nd_mask = MT8196_PWR_ACK_2ND,
		.ctl_offs = 0xe00,
		.pwr_sta_offs = 0xe00,
		.pwr_sta2nd_offs = 0xe00,
		.ext_buck_iso_offs = 0xefc,
		.ext_buck_iso_mask = GENMASK(1, 0),
		.bp_cfg = {
			BUS_PROT_WR_IGN(INFRA, MT8196_TOP_AXI_PROT_EN_SLEEP0_MD,
					MT8196_TOP_AXI_PROT_EN_SET,
					MT8196_TOP_AXI_PROT_EN_CLR,
					MT8196_TOP_AXI_PROT_EN_STA),
			BUS_PROT_WR_IGN(INFRA, MT8196_TOP_AXI_PROT_EN_1_SLEEP1_MD,
					MT8196_TOP_AXI_PROT_EN_1_SET,
					MT8196_TOP_AXI_PROT_EN_1_CLR,
					MT8196_TOP_AXI_PROT_EN_1_STA),
		},
		.caps = MTK_SCPD_MODEM_PWRSEQ | MTK_SCPD_EXT_BUCK_ISO |
			MTK_SCPD_SKIP_RESET_B | MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT8196_POWER_DOMAIN_CONN] = {
		.name = "conn",
		.sta_mask = MT8196_PWR_ACK,
		.sta2nd_mask = MT8196_PWR_ACK_2ND,
		.ctl_offs = 0xe04,
		.pwr_sta_offs = 0xe04,
		.pwr_sta2nd_offs = 0xe04,
		.bp_cfg = {
			BUS_PROT_WR_IGN(SPM, MT8196_SPM_PROT_EN_BUS_CONN,
					MT8196_SPM_BUS_PROTECT_CON_SET,
					MT8196_SPM_BUS_PROTECT_CON_CLR,
					MT8196_SPM_BUS_PROTECT_RDY),
		},
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
		.rtff_type = SCPSYS_RTFF_TYPE_GENERIC,
	},
	[MT8196_POWER_DOMAIN_SSUSB_DP_PHY_P0] = {
		.name = "ssusb-dp-phy-p0",
		.sta_mask = MT8196_PWR_ACK,
		.sta2nd_mask = MT8196_PWR_ACK_2ND,
		.ctl_offs = 0xe18,
		.pwr_sta_offs = 0xe18,
		.pwr_sta2nd_offs = 0xe18,
		.bp_cfg = {
			BUS_PROT_WR_IGN(SPM, MT8196_SPM_PROT_EN_BUS_SSUSB_DP_PHY_P0,
					MT8196_SPM_BUS_PROTECT_CON_SET,
					MT8196_SPM_BUS_PROTECT_CON_CLR,
					MT8196_SPM_BUS_PROTECT_RDY),
		},
		.caps = MTK_SCPD_ALWAYS_ON,
		.rtff_type = SCPSYS_RTFF_TYPE_GENERIC,
	},
	[MT8196_POWER_DOMAIN_SSUSB_P0] = {
		.name = "ssusb-p0",
		.sta_mask = MT8196_PWR_ACK,
		.sta2nd_mask = MT8196_PWR_ACK_2ND,
		.ctl_offs = 0xe1c,
		.pwr_sta_offs = 0xe1c,
		.pwr_sta2nd_offs = 0xe1c,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR_IGN(SPM, MT8196_SPM_PROT_EN_BUS_SSUSB_P0,
					MT8196_SPM_BUS_PROTECT_CON_SET,
					MT8196_SPM_BUS_PROTECT_CON_CLR,
					MT8196_SPM_BUS_PROTECT_RDY),
		},
		.caps = MTK_SCPD_ALWAYS_ON,
		.rtff_type = SCPSYS_RTFF_TYPE_GENERIC,
	},
	[MT8196_POWER_DOMAIN_SSUSB_P1] = {
		.name = "ssusb-p1",
		.sta_mask = MT8196_PWR_ACK,
		.sta2nd_mask = MT8196_PWR_ACK_2ND,
		.ctl_offs = 0xe20,
		.pwr_sta_offs = 0xe20,
		.pwr_sta2nd_offs = 0xe20,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR_IGN(SPM, MT8196_SPM_PROT_EN_BUS_SSUSB_P1,
					MT8196_SPM_BUS_PROTECT_CON_SET,
					MT8196_SPM_BUS_PROTECT_CON_CLR,
					MT8196_SPM_BUS_PROTECT_RDY),
		},
		.caps = MTK_SCPD_ALWAYS_ON,
		.rtff_type = SCPSYS_RTFF_TYPE_GENERIC,
	},
	[MT8196_POWER_DOMAIN_SSUSB_P23] = {
		.name = "ssusb-p23",
		.sta_mask = MT8196_PWR_ACK,
		.sta2nd_mask = MT8196_PWR_ACK_2ND,
		.ctl_offs = 0xe24,
		.pwr_sta_offs = 0xe24,
		.pwr_sta2nd_offs = 0xe24,
		.bp_cfg = {
			BUS_PROT_WR_IGN(SPM, MT8196_SPM_PROT_EN_BUS_SSUSB_P23,
					MT8196_SPM_BUS_PROTECT_CON_SET,
					MT8196_SPM_BUS_PROTECT_CON_CLR,
					MT8196_SPM_BUS_PROTECT_RDY),
		},
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
		.rtff_type = SCPSYS_RTFF_TYPE_GENERIC,
	},
	[MT8196_POWER_DOMAIN_SSUSB_PHY_P2] = {
		.name = "ssusb-phy-p2",
		.sta_mask = MT8196_PWR_ACK,
		.sta2nd_mask = MT8196_PWR_ACK_2ND,
		.ctl_offs = 0xe28,
		.pwr_sta_offs = 0xe28,
		.pwr_sta2nd_offs = 0xe28,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR_IGN(SPM, MT8196_SPM_PROT_EN_BUS_SSUSB_PHY_P2,
					MT8196_SPM_BUS_PROTECT_CON_SET,
					MT8196_SPM_BUS_PROTECT_CON_CLR,
					MT8196_SPM_BUS_PROTECT_RDY),
		},
		.caps = MTK_SCPD_KEEP_DEFAULT_OFF,
		.rtff_type = SCPSYS_RTFF_TYPE_GENERIC,
	},
	[MT8196_POWER_DOMAIN_PEXTP_MAC0] = {
		.name = "pextp-mac0",
		.sta_mask = MT8196_PWR_ACK,
		.sta2nd_mask = MT8196_PWR_ACK_2ND,
		.ctl_offs = 0xe34,
		.pwr_sta_offs = 0xe34,
		.pwr_sta2nd_offs = 0xe34,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR_IGN(SPM, MT8196_SPM_PROT_EN_BUS_PEXTP_MAC0,
					MT8196_SPM_BUS_PROTECT_CON_SET,
					MT8196_SPM_BUS_PROTECT_CON_CLR,
					MT8196_SPM_BUS_PROTECT_RDY),
		},
		.rtff_type = SCPSYS_RTFF_TYPE_PCIE_PHY,
	},
	[MT8196_POWER_DOMAIN_PEXTP_MAC1] = {
		.name = "pextp-mac1",
		.sta_mask = MT8196_PWR_ACK,
		.sta2nd_mask = MT8196_PWR_ACK_2ND,
		.ctl_offs = 0xe38,
		.pwr_sta_offs = 0xe38,
		.pwr_sta2nd_offs = 0xe38,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR_IGN(SPM, MT8196_SPM_PROT_EN_BUS_PEXTP_MAC1,
					MT8196_SPM_BUS_PROTECT_CON_SET,
					MT8196_SPM_BUS_PROTECT_CON_CLR,
					MT8196_SPM_BUS_PROTECT_RDY),
		},
		.rtff_type = SCPSYS_RTFF_TYPE_PCIE_PHY,
	},
	[MT8196_POWER_DOMAIN_PEXTP_MAC2] = {
		.name = "pextp-mac2",
		.sta_mask = MT8196_PWR_ACK,
		.sta2nd_mask = MT8196_PWR_ACK_2ND,
		.ctl_offs = 0xe3c,
		.pwr_sta_offs = 0xe3c,
		.pwr_sta2nd_offs = 0xe3c,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR_IGN(SPM, MT8196_SPM_PROT_EN_BUS_PEXTP_MAC2,
					MT8196_SPM_BUS_PROTECT_CON_SET,
					MT8196_SPM_BUS_PROTECT_CON_CLR,
					MT8196_SPM_BUS_PROTECT_RDY),
		},
		.rtff_type = SCPSYS_RTFF_TYPE_PCIE_PHY,
	},
	[MT8196_POWER_DOMAIN_PEXTP_PHY0] = {
		.name = "pextp-phy0",
		.sta_mask = MT8196_PWR_ACK,
		.sta2nd_mask = MT8196_PWR_ACK_2ND,
		.ctl_offs = 0xe40,
		.pwr_sta_offs = 0xe40,
		.pwr_sta2nd_offs = 0xe40,
		.bp_cfg = {
			BUS_PROT_WR_IGN(SPM, MT8196_SPM_PROT_EN_BUS_PEXTP_PHY0,
					MT8196_SPM_BUS_PROTECT_CON_SET,
					MT8196_SPM_BUS_PROTECT_CON_CLR,
					MT8196_SPM_BUS_PROTECT_RDY),
		},
		.rtff_type = SCPSYS_RTFF_TYPE_PCIE_PHY,
	},
	[MT8196_POWER_DOMAIN_PEXTP_PHY1] = {
		.name = "pextp-phy1",
		.sta_mask = MT8196_PWR_ACK,
		.sta2nd_mask = MT8196_PWR_ACK_2ND,
		.ctl_offs = 0xe44,
		.pwr_sta_offs = 0xe44,
		.pwr_sta2nd_offs = 0xe44,
		.bp_cfg = {
			BUS_PROT_WR_IGN(SPM, MT8196_SPM_PROT_EN_BUS_PEXTP_PHY1,
					MT8196_SPM_BUS_PROTECT_CON_SET,
					MT8196_SPM_BUS_PROTECT_CON_CLR,
					MT8196_SPM_BUS_PROTECT_RDY),
		},
		.rtff_type = SCPSYS_RTFF_TYPE_PCIE_PHY,
	},
	[MT8196_POWER_DOMAIN_PEXTP_PHY2] = {
		.name = "pextp-phy2",
		.sta_mask = MT8196_PWR_ACK,
		.sta2nd_mask = MT8196_PWR_ACK_2ND,
		.ctl_offs = 0xe48,
		.pwr_sta_offs = 0xe48,
		.pwr_sta2nd_offs = 0xe48,
		.bp_cfg = {
			BUS_PROT_WR_IGN(SPM, MT8196_SPM_PROT_EN_BUS_PEXTP_PHY2,
					MT8196_SPM_BUS_PROTECT_CON_SET,
					MT8196_SPM_BUS_PROTECT_CON_CLR,
					MT8196_SPM_BUS_PROTECT_RDY),
		},
		.rtff_type = SCPSYS_RTFF_TYPE_PCIE_PHY,
	},
	[MT8196_POWER_DOMAIN_AUDIO] = {
		.name = "audio",
		.sta_mask = MT8196_PWR_ACK,
		.sta2nd_mask = MT8196_PWR_ACK_2ND,
		.ctl_offs = 0xe4c,
		.pwr_sta_offs = 0xe4c,
		.pwr_sta2nd_offs = 0xe4c,
		.sram_pdn_bits = BIT(8),
		.sram_pdn_ack_bits = BIT(12),
		.bp_cfg = {
			BUS_PROT_WR_IGN(SPM, MT8196_SPM_PROT_EN_BUS_AUDIO,
					MT8196_SPM_BUS_PROTECT_CON_SET,
					MT8196_SPM_BUS_PROTECT_CON_CLR,
					MT8196_SPM_BUS_PROTECT_RDY),
		},
		.rtff_type = SCPSYS_RTFF_TYPE_GENERIC,
	},
	[MT8196_POWER_DOMAIN_ADSP_TOP_DORMANT] = {
		.name = "adsp-top-dormant",
		.sta_mask = MT8196_PWR_ACK,
		.sta2nd_mask = MT8196_PWR_ACK_2ND,
		.ctl_offs = 0xe54,
		.pwr_sta_offs = 0xe54,
		.pwr_sta2nd_offs = 0xe54,
		/* Note: This is not managing powerdown (pdn), but sleep instead (slp) */
		.sram_pdn_bits = BIT(9),
		.sram_pdn_ack_bits = BIT(13),
		.bp_cfg = {
			BUS_PROT_WR_IGN(SPM, MT8196_SPM_PROT_EN_BUS_ADSP_TOP,
					MT8196_SPM_BUS_PROTECT_CON_SET,
					MT8196_SPM_BUS_PROTECT_CON_CLR,
					MT8196_SPM_BUS_PROTECT_RDY),
		},
		.caps = MTK_SCPD_SRAM_ISO | MTK_SCPD_SRAM_PDN_INVERTED,
	},
	[MT8196_POWER_DOMAIN_ADSP_INFRA] = {
		.name = "adsp-infra",
		.sta_mask = MT8196_PWR_ACK,
		.sta2nd_mask = MT8196_PWR_ACK_2ND,
		.ctl_offs = 0xe58,
		.pwr_sta_offs = 0xe58,
		.pwr_sta2nd_offs = 0xe58,
		.bp_cfg = {
			BUS_PROT_WR_IGN(SPM, MT8196_SPM_PROT_EN_BUS_ADSP_INFRA,
					MT8196_SPM_BUS_PROTECT_CON_SET,
					MT8196_SPM_BUS_PROTECT_CON_CLR,
					MT8196_SPM_BUS_PROTECT_RDY),
		},
		.caps = MTK_SCPD_ALWAYS_ON,
		.rtff_type = SCPSYS_RTFF_TYPE_GENERIC,
	},
	[MT8196_POWER_DOMAIN_ADSP_AO] = {
		.name = "adsp-ao",
		.sta_mask = MT8196_PWR_ACK,
		.sta2nd_mask = MT8196_PWR_ACK_2ND,
		.ctl_offs = 0xe5c,
		.pwr_sta_offs = 0xe5c,
		.pwr_sta2nd_offs = 0xe5c,
		.bp_cfg = {
			BUS_PROT_WR_IGN(SPM, MT8196_SPM_PROT_EN_BUS_ADSP_AO,
					MT8196_SPM_BUS_PROTECT_CON_SET,
					MT8196_SPM_BUS_PROTECT_CON_CLR,
					MT8196_SPM_BUS_PROTECT_RDY),
		},
		.caps = MTK_SCPD_ALWAYS_ON,
		.rtff_type = SCPSYS_RTFF_TYPE_GENERIC,
	},
};

static const struct scpsys_hwv_domain_data scpsys_hwv_domain_data_mt8196[] = {
	[MT8196_POWER_DOMAIN_MM_PROC_DORMANT] = {
		.name = "mm-proc-dormant",
		.set = 0x0218,
		.clr = 0x021c,
		.done = 0x141c,
		.en = 0x1410,
		.set_sta = 0x146c,
		.clr_sta = 0x1470,
		.setclr_bit = 0,
		.caps = MTK_SCPD_ALWAYS_ON,
	},
	[MT8196_POWER_DOMAIN_SSR] = {
		.name = "ssrsys",
		.set = 0x0218,
		.clr = 0x021c,
		.done = 0x141c,
		.en = 0x1410,
		.set_sta = 0x146c,
		.clr_sta = 0x1470,
		.setclr_bit = 1,
	},
};

static const struct scpsys_hwv_domain_data hfrpsys_hwv_domain_data_mt8196[] = {
	[MT8196_POWER_DOMAIN_VDE0] = {
		.name = "vde0",
		.set = 0x0218,
		.clr = 0x021C,
		.done = 0x141C,
		.en = 0x1410,
		.set_sta = 0x146C,
		.clr_sta = 0x1470,
		.setclr_bit = 7,
	},
	[MT8196_POWER_DOMAIN_VDE1] = {
		.name = "vde1",
		.set = 0x0218,
		.clr = 0x021C,
		.done = 0x141C,
		.en = 0x1410,
		.set_sta = 0x146C,
		.clr_sta = 0x1470,
		.setclr_bit = 8,
	},
	[MT8196_POWER_DOMAIN_VDE_VCORE0] = {
		.name = "vde-vcore0",
		.set = 0x0218,
		.clr = 0x021C,
		.done = 0x141C,
		.en = 0x1410,
		.set_sta = 0x146C,
		.clr_sta = 0x1470,
		.setclr_bit = 9,
	},
	[MT8196_POWER_DOMAIN_VEN0] = {
		.name = "ven0",
		.set = 0x0218,
		.clr = 0x021C,
		.done = 0x141C,
		.en = 0x1410,
		.set_sta = 0x146C,
		.clr_sta = 0x1470,
		.setclr_bit = 10,
	},
	[MT8196_POWER_DOMAIN_VEN1] = {
		.name = "ven1",
		.set = 0x0218,
		.clr = 0x021C,
		.done = 0x141C,
		.en = 0x1410,
		.set_sta = 0x146C,
		.clr_sta = 0x1470,
		.setclr_bit = 11,
	},
	[MT8196_POWER_DOMAIN_VEN2] = {
		.name = "ven2",
		.set = 0x0218,
		.clr = 0x021C,
		.done = 0x141C,
		.en = 0x1410,
		.set_sta = 0x146C,
		.clr_sta = 0x1470,
		.setclr_bit = 12,
	},
	[MT8196_POWER_DOMAIN_DISP_VCORE] = {
		.name = "disp-vcore",
		.set = 0x0218,
		.clr = 0x021C,
		.done = 0x141C,
		.en = 0x1410,
		.set_sta = 0x146C,
		.clr_sta = 0x1470,
		.setclr_bit = 24,
	},
	[MT8196_POWER_DOMAIN_DIS0_DORMANT] = {
		.name = "dis0-dormant",
		.set = 0x0218,
		.clr = 0x021C,
		.done = 0x141C,
		.en = 0x1410,
		.set_sta = 0x146C,
		.clr_sta = 0x1470,
		.setclr_bit = 25,
	},
	[MT8196_POWER_DOMAIN_DIS1_DORMANT] = {
		.name = "dis1-dormant",
		.set = 0x0218,
		.clr = 0x021C,
		.done = 0x141C,
		.en = 0x1410,
		.set_sta = 0x146C,
		.clr_sta = 0x1470,
		.setclr_bit = 26,
	},
	[MT8196_POWER_DOMAIN_OVL0_DORMANT] = {
		.name = "ovl0-dormant",
		.set = 0x0218,
		.clr = 0x021C,
		.done = 0x141C,
		.en = 0x1410,
		.set_sta = 0x146C,
		.clr_sta = 0x1470,
		.setclr_bit = 27,
	},
	[MT8196_POWER_DOMAIN_OVL1_DORMANT] = {
		.name = "ovl1-dormant",
		.set = 0x0218,
		.clr = 0x021C,
		.done = 0x141C,
		.en = 0x1410,
		.set_sta = 0x146C,
		.clr_sta = 0x1470,
		.setclr_bit = 28,
	},
	[MT8196_POWER_DOMAIN_DISP_EDPTX_DORMANT] = {
		.name = "disp-edptx-dormant",
		.set = 0x0218,
		.clr = 0x021C,
		.done = 0x141C,
		.en = 0x1410,
		.set_sta = 0x146C,
		.clr_sta = 0x1470,
		.setclr_bit = 29,
	},
	[MT8196_POWER_DOMAIN_DISP_DPTX_DORMANT] = {
		.name = "disp-dptx-dormant",
		.set = 0x0218,
		.clr = 0x021C,
		.done = 0x141C,
		.en = 0x1410,
		.set_sta = 0x146C,
		.clr_sta = 0x1470,
		.setclr_bit = 30,
	},
	[MT8196_POWER_DOMAIN_MML0_SHUTDOWN] = {
		.name = "mml0-shutdown",
		.set = 0x0218,
		.clr = 0x021C,
		.done = 0x141C,
		.en = 0x1410,
		.set_sta = 0x146C,
		.clr_sta = 0x1470,
		.setclr_bit = 31,
	},
	[MT8196_POWER_DOMAIN_MML1_SHUTDOWN] = {
		.name = "mml1-shutdown",
		.set = 0x0220,
		.clr = 0x0224,
		.done = 0x142C,
		.en = 0x1420,
		.set_sta = 0x1474,
		.clr_sta = 0x1478,
		.setclr_bit = 0,
	},
	[MT8196_POWER_DOMAIN_MM_INFRA0] = {
		.name = "mm-infra0",
		.set = 0x0220,
		.clr = 0x0224,
		.done = 0x142C,
		.en = 0x1420,
		.set_sta = 0x1474,
		.clr_sta = 0x1478,
		.setclr_bit = 1,
	},
	[MT8196_POWER_DOMAIN_MM_INFRA1] = {
		.name = "mm-infra1",
		.set = 0x0220,
		.clr = 0x0224,
		.done = 0x142C,
		.en = 0x1420,
		.set_sta = 0x1474,
		.clr_sta = 0x1478,
		.setclr_bit = 2,
	},
	[MT8196_POWER_DOMAIN_MM_INFRA_AO] = {
		.name = "mm-infra-ao",
		.set = 0x0220,
		.clr = 0x0224,
		.done = 0x142C,
		.en = 0x1420,
		.set_sta = 0x1474,
		.clr_sta = 0x1478,
		.setclr_bit = 3,
	},
	[MT8196_POWER_DOMAIN_CSI_BS_RX] = {
		.name = "csi-bs-rx",
		.set = 0x0220,
		.clr = 0x0224,
		.done = 0x142C,
		.en = 0x1420,
		.set_sta = 0x1474,
		.clr_sta = 0x1478,
		.setclr_bit = 5,
	},
	[MT8196_POWER_DOMAIN_CSI_LS_RX] = {
		.name = "csi-ls-rx",
		.set = 0x0220,
		.clr = 0x0224,
		.done = 0x142C,
		.en = 0x1420,
		.set_sta = 0x1474,
		.clr_sta = 0x1478,
		.setclr_bit = 6,
	},
	[MT8196_POWER_DOMAIN_DSI_PHY0] = {
		.name = "dsi-phy0",
		.set = 0x0220,
		.clr = 0x0224,
		.done = 0x142C,
		.en = 0x1420,
		.set_sta = 0x1474,
		.clr_sta = 0x1478,
		.setclr_bit = 7,
	},
	[MT8196_POWER_DOMAIN_DSI_PHY1] = {
		.name = "dsi-phy1",
		.set = 0x0220,
		.clr = 0x0224,
		.done = 0x142C,
		.en = 0x1420,
		.set_sta = 0x1474,
		.clr_sta = 0x1478,
		.setclr_bit = 8,
	},
	[MT8196_POWER_DOMAIN_DSI_PHY2] = {
		.name = "dsi-phy2",
		.set = 0x0220,
		.clr = 0x0224,
		.done = 0x142C,
		.en = 0x1420,
		.set_sta = 0x1474,
		.clr_sta = 0x1478,
		.setclr_bit = 9,
	},
};

static const struct scpsys_soc_data mt8196_scpsys_data = {
	.domains_data = scpsys_domain_data_mt8196,
	.num_domains = ARRAY_SIZE(scpsys_domain_data_mt8196),
	.bus_prot_blocks = scpsys_bus_prot_blocks_mt8196,
	.num_bus_prot_blocks = ARRAY_SIZE(scpsys_bus_prot_blocks_mt8196),
	.type = SCPSYS_MTCMOS_TYPE_DIRECT_CTL,
};

static const struct scpsys_soc_data mt8196_scpsys_hwv_data = {
	.hwv_domains_data = scpsys_hwv_domain_data_mt8196,
	.num_hwv_domains = ARRAY_SIZE(scpsys_hwv_domain_data_mt8196),
	.type = SCPSYS_MTCMOS_TYPE_HW_VOTER,
};

static const struct scpsys_soc_data mt8196_hfrpsys_hwv_data = {
	.hwv_domains_data = hfrpsys_hwv_domain_data_mt8196,
	.num_hwv_domains = ARRAY_SIZE(hfrpsys_hwv_domain_data_mt8196),
	.type = SCPSYS_MTCMOS_TYPE_HW_VOTER,
};

#endif /* __SOC_MEDIATEK_MT8196_PM_DOMAINS_H */
