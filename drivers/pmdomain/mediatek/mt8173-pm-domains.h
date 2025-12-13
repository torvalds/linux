/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOC_MEDIATEK_MT8173_PM_DOMAINS_H
#define __SOC_MEDIATEK_MT8173_PM_DOMAINS_H

#include "mtk-pm-domains.h"
#include <dt-bindings/power/mt8173-power.h>

/*
 * MT8173 power domain support
 */
static enum scpsys_bus_prot_block scpsys_bus_prot_blocks_mt8173[] = {
	BUS_PROT_BLOCK_INFRA
};

static const struct scpsys_domain_data scpsys_domain_data_mt8173[] = {
	[MT8173_POWER_DOMAIN_VDEC] = {
		.name = "vdec",
		.sta_mask = PWR_STATUS_VDEC,
		.ctl_offs = SPM_VDE_PWR_CON,
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
	},
	[MT8173_POWER_DOMAIN_VENC] = {
		.name = "venc",
		.sta_mask = PWR_STATUS_VENC,
		.ctl_offs = SPM_VEN_PWR_CON,
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
	},
	[MT8173_POWER_DOMAIN_ISP] = {
		.name = "isp",
		.sta_mask = PWR_STATUS_ISP,
		.ctl_offs = SPM_ISP_PWR_CON,
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(13, 12),
	},
	[MT8173_POWER_DOMAIN_MM] = {
		.name = "mm",
		.sta_mask = PWR_STATUS_DISP,
		.ctl_offs = SPM_DIS_PWR_CON,
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.bp_cfg = {
			BUS_PROT_INFRA_UPDATE_TOPAXI(MT8173_TOP_AXI_PROT_EN_MM_M0 |
						     MT8173_TOP_AXI_PROT_EN_MM_M1),
		},
	},
	[MT8173_POWER_DOMAIN_VENC_LT] = {
		.name = "venc_lt",
		.sta_mask = PWR_STATUS_VENC_LT,
		.ctl_offs = SPM_VEN2_PWR_CON,
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
	},
	[MT8173_POWER_DOMAIN_AUDIO] = {
		.name = "audio",
		.sta_mask = PWR_STATUS_AUDIO,
		.ctl_offs = SPM_AUDIO_PWR_CON,
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
	},
	[MT8173_POWER_DOMAIN_USB] = {
		.name = "usb",
		.sta_mask = PWR_STATUS_USB,
		.ctl_offs = SPM_USB_PWR_CON,
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT8173_POWER_DOMAIN_MFG_ASYNC] = {
		.name = "mfg_async",
		.sta_mask = PWR_STATUS_MFG_ASYNC,
		.ctl_offs = SPM_MFG_ASYNC_PWR_CON,
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = 0,
		.caps = MTK_SCPD_DOMAIN_SUPPLY,
	},
	[MT8173_POWER_DOMAIN_MFG_2D] = {
		.name = "mfg_2d",
		.sta_mask = PWR_STATUS_MFG_2D,
		.ctl_offs = SPM_MFG_2D_PWR_CON,
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(13, 12),
	},
	[MT8173_POWER_DOMAIN_MFG] = {
		.name = "mfg",
		.sta_mask = PWR_STATUS_MFG,
		.ctl_offs = SPM_MFG_PWR_CON,
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = GENMASK(13, 8),
		.sram_pdn_ack_bits = GENMASK(21, 16),
		.bp_cfg = {
			BUS_PROT_INFRA_UPDATE_TOPAXI(MT8173_TOP_AXI_PROT_EN_MFG_S |
						     MT8173_TOP_AXI_PROT_EN_MFG_M0 |
						     MT8173_TOP_AXI_PROT_EN_MFG_M1 |
						     MT8173_TOP_AXI_PROT_EN_MFG_SNOOP_OUT),
		},
	},
};

static const struct scpsys_soc_data mt8173_scpsys_data = {
	.domains_data = scpsys_domain_data_mt8173,
	.num_domains = ARRAY_SIZE(scpsys_domain_data_mt8173),
	.bus_prot_blocks = scpsys_bus_prot_blocks_mt8173,
	.num_bus_prot_blocks = ARRAY_SIZE(scpsys_bus_prot_blocks_mt8173),
};

#endif /* __SOC_MEDIATEK_MT8173_PM_DOMAINS_H */
