/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOC_MEDIATEK_MT8365_PM_DOMAINS_H
#define __SOC_MEDIATEK_MT8365_PM_DOMAINS_H

#include "mtk-pm-domains.h"
#include <dt-bindings/power/mediatek,mt8365-power.h>

/*
 * MT8365 power domain support
 */

#define MT8365_BUS_PROT_INFRA_WR_TOPAXI(_mask)				\
		BUS_PROT_WR(INFRA, _mask,				\
			    MT8365_INFRA_TOPAXI_PROTECTEN_SET,		\
			    MT8365_INFRA_TOPAXI_PROTECTEN_CLR,		\
			    MT8365_INFRA_TOPAXI_PROTECTEN_STA1)

#define MT8365_BUS_PROT_INFRA_WR_TOPAXI_1(_mask)			\
		BUS_PROT_WR(INFRA, _mask,				\
			    MT8365_INFRA_TOPAXI_PROTECTEN_1_SET,	\
			    MT8365_INFRA_TOPAXI_PROTECTEN_1_CLR,	\
			    MT8365_INFRA_TOPAXI_PROTECTEN_1_STA1)

#define MT8365_BUS_PROT_SMI_WR_CLAMP_EN_PORT(port)			\
		BUS_PROT_WR(SMI, BIT(port),				\
			    MT8365_SMI_COMMON_CLAMP_EN_SET,		\
			    MT8365_SMI_COMMON_CLAMP_EN_CLR,		\
			    MT8365_SMI_COMMON_CLAMP_EN)

#define MT8365_BUS_PROT_WAY_EN(_set_mask, _set, _sta_mask, _sta)	\
		_BUS_PROT(_set_mask, _set, _set, _sta_mask, _sta,	\
			  BUS_PROT_COMPONENT_INFRA |			\
			  BUS_PROT_STA_COMPONENT_INFRA_NAO |		\
			  BUS_PROT_INVERTED |				\
			  BUS_PROT_REG_UPDATE)

static const struct scpsys_domain_data scpsys_domain_data_mt8365[] = {
	[MT8365_POWER_DOMAIN_MM] = {
		.name = "mm",
		.sta_mask = PWR_STATUS_DISP,
		.ctl_offs = 0x30c,
		.pwr_sta_offs = 0x0180,
		.pwr_sta2nd_offs = 0x0184,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.bp_cfg = {
			MT8365_BUS_PROT_INFRA_WR_TOPAXI_1(
				MT8365_INFRA_TOPAXI_PROTECTEN_1_MM2INFRA_AXI_GALS_MST_0 |
				MT8365_INFRA_TOPAXI_PROTECTEN_1_MM2INFRA_AXI_GALS_MST_1),
			MT8365_BUS_PROT_INFRA_WR_TOPAXI(
				MT8365_INFRA_TOPAXI_PROTECTEN_MM_M0 |
				MT8365_INFRA_TOPAXI_PROTECTEN_MDMCU_M1 |
				MT8365_INFRA_TOPAXI_PROTECTEN_MM2INFRA_AXI_GALS_SLV_0 |
				MT8365_INFRA_TOPAXI_PROTECTEN_MM2INFRA_AXI_GALS_SLV_1),
			MT8365_BUS_PROT_WAY_EN(
				MT8365_INFRA_TOPAXI_SI0_WAY_EN_MMAPB_S,
				MT8365_INFRA_TOPAXI_SI0_CTL,
				MT8365_INFRA_NAO_TOPAXI_SI0_CTRL_UPDATED,
				MT8365_INFRA_NAO_TOPAXI_SI0_STA),
			MT8365_BUS_PROT_WAY_EN(
				MT8365_INFRA_TOPAXI_SI2_WAY_EN_PERI_M1,
				MT8365_INFRA_TOPAXI_SI2_CTL,
				MT8365_INFRA_NAO_TOPAXI_SI2_CTRL_UPDATED,
				MT8365_INFRA_NAO_TOPAXI_SI2_STA),
			MT8365_BUS_PROT_INFRA_WR_TOPAXI(
				MT8365_INFRA_TOPAXI_PROTECTEN_MMAPB_S),
		},
		.caps = MTK_SCPD_STRICT_BUS_PROTECTION | MTK_SCPD_HAS_INFRA_NAO,
	},
	[MT8365_POWER_DOMAIN_VENC] = {
		.name = "venc",
		.sta_mask = PWR_STATUS_VENC,
		.ctl_offs = 0x0304,
		.pwr_sta_offs = 0x0180,
		.pwr_sta2nd_offs = 0x0184,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.bp_cfg = {
			MT8365_BUS_PROT_SMI_WR_CLAMP_EN_PORT(1),
		},
	},
	[MT8365_POWER_DOMAIN_AUDIO] = {
		.name = "audio",
		.sta_mask = PWR_STATUS_AUDIO,
		.ctl_offs = 0x0314,
		.pwr_sta_offs = 0x0180,
		.pwr_sta2nd_offs = 0x0184,
		.sram_pdn_bits = GENMASK(12, 8),
		.sram_pdn_ack_bits = GENMASK(17, 13),
		.bp_cfg = {
			MT8365_BUS_PROT_INFRA_WR_TOPAXI_1(
				MT8365_INFRA_TOPAXI_PROTECTEN_1_PWRDNREQ_MP1_L2C_AFIFO |
				MT8365_INFRA_TOPAXI_PROTECTEN_1_AUDIO_BUS_AUDIO_M),
		},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT8365_POWER_DOMAIN_CONN] = {
		.name = "conn",
		.sta_mask = PWR_STATUS_CONN,
		.ctl_offs = 0x032c,
		.pwr_sta_offs = 0x0180,
		.pwr_sta2nd_offs = 0x0184,
		.sram_pdn_bits = 0,
		.sram_pdn_ack_bits = 0,
		.bp_cfg = {
			MT8365_BUS_PROT_INFRA_WR_TOPAXI(
				MT8365_INFRA_TOPAXI_PROTECTEN_AP2CONN_AHB),
			MT8365_BUS_PROT_INFRA_WR_TOPAXI_1(
				MT8365_INFRA_TOPAXI_PROTECTEN_1_CONN2INFRA_AXI_GALS_MST),
			MT8365_BUS_PROT_INFRA_WR_TOPAXI(
				MT8365_INFRA_TOPAXI_PROTECTEN_CONN2INFRA_AHB),
			MT8365_BUS_PROT_INFRA_WR_TOPAXI_1(
				MT8365_INFRA_TOPAXI_PROTECTEN_1_INFRA2CONN_AHB_GALS_SLV),
		},
		.caps = MTK_SCPD_ACTIVE_WAKEUP | MTK_SCPD_KEEP_DEFAULT_OFF,
	},
	[MT8365_POWER_DOMAIN_MFG] = {
		.name = "mfg",
		.sta_mask = PWR_STATUS_MFG,
		.ctl_offs = 0x0338,
		.pwr_sta_offs = 0x0180,
		.pwr_sta2nd_offs = 0x0184,
		.sram_pdn_bits = GENMASK(9, 8),
		.sram_pdn_ack_bits = GENMASK(13, 12),
		.bp_cfg = {
			MT8365_BUS_PROT_INFRA_WR_TOPAXI(BIT(25)),
			MT8365_BUS_PROT_INFRA_WR_TOPAXI(
				MT8365_INFRA_TOPAXI_PROTECTEN_MFG_M0 |
				MT8365_INFRA_TOPAXI_PROTECTEN_INFRA2MFG),
		},
	},
	[MT8365_POWER_DOMAIN_CAM] = {
		.name = "cam",
		.sta_mask = BIT(25),
		.ctl_offs = 0x0344,
		.pwr_sta_offs = 0x0180,
		.pwr_sta2nd_offs = 0x0184,
		.sram_pdn_bits = GENMASK(9, 8),
		.sram_pdn_ack_bits = GENMASK(13, 12),
		.bp_cfg = {
			MT8365_BUS_PROT_INFRA_WR_TOPAXI_1(
				MT8365_INFRA_TOPAXI_PROTECTEN_1_CAM2MM_AXI_GALS_MST),
			MT8365_BUS_PROT_SMI_WR_CLAMP_EN_PORT(2),
		},
	},
	[MT8365_POWER_DOMAIN_VDEC] = {
		.name = "vdec",
		.sta_mask = BIT(31),
		.ctl_offs = 0x0370,
		.pwr_sta_offs = 0x0180,
		.pwr_sta2nd_offs = 0x0184,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.bp_cfg = {
			MT8365_BUS_PROT_SMI_WR_CLAMP_EN_PORT(3),
		},
	},
	[MT8365_POWER_DOMAIN_APU] = {
		.name = "apu",
		.sta_mask = BIT(16),
		.ctl_offs = 0x0378,
		.pwr_sta_offs = 0x0180,
		.pwr_sta2nd_offs = 0x0184,
		.sram_pdn_bits = GENMASK(14, 8),
		.sram_pdn_ack_bits = GENMASK(21, 15),
		.bp_cfg = {
			MT8365_BUS_PROT_INFRA_WR_TOPAXI_1(
				MT8365_INFRA_TOPAXI_PROTECTEN_1_APU2AP |
				MT8365_INFRA_TOPAXI_PROTECTEN_1_APU_CBIP_GALS_MST),
			MT8365_BUS_PROT_SMI_WR_CLAMP_EN_PORT(4),
		},
	},
	[MT8365_POWER_DOMAIN_DSP] = {
		.name = "dsp",
		.sta_mask = BIT(17),
		.ctl_offs = 0x037C,
		.pwr_sta_offs = 0x0180,
		.pwr_sta2nd_offs = 0x0184,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
		.bp_cfg = {
			MT8365_BUS_PROT_INFRA_WR_TOPAXI_1(
				MT8365_INFRA_TOPAXI_PROTECTEN_1_PWRDNREQ_INFRA_GALS_ADB |
				MT8365_INFRA_TOPAXI_PROTECTEN_1_AUDIO_BUS_DSP_M |
				MT8365_INFRA_TOPAXI_PROTECTEN_1_AUDIO_BUS_DSP_S),
		},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
};

static const struct scpsys_soc_data mt8365_scpsys_data = {
	.domains_data = scpsys_domain_data_mt8365,
	.num_domains = ARRAY_SIZE(scpsys_domain_data_mt8365),
};

#endif /* __SOC_MEDIATEK_MT8365_PM_DOMAINS_H */
