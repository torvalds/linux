/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOC_MEDIATEK_MT8167_PM_DOMAINS_H
#define __SOC_MEDIATEK_MT8167_PM_DOMAINS_H

#include "mtk-pm-domains.h"
#include <dt-bindings/power/mt8167-power.h>

#define MT8167_PWR_STATUS_MFG_2D	BIT(24)
#define MT8167_PWR_STATUS_MFG_ASYNC	BIT(25)

/*
 * MT8167 power domain support
 */

static const struct scpsys_domain_data scpsys_domain_data_mt8167[] = {
	[MT8167_POWER_DOMAIN_MM] = {
		.name = "mm",
		.sta_mask = PWR_STATUS_DISP,
		.ctl_offs = SPM_DIS_PWR_CON,
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.bp_cfg = {
			BUS_PROT_INFRA_UPDATE_TOPAXI(MT8167_TOP_AXI_PROT_EN_MM_EMI |
						     MT8167_TOP_AXI_PROT_EN_MCU_MM),
		},
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT8167_POWER_DOMAIN_VDEC] = {
		.name = "vdec",
		.sta_mask = PWR_STATUS_VDEC,
		.ctl_offs = SPM_VDE_PWR_CON,
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = GENMASK(12, 12),
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT8167_POWER_DOMAIN_ISP] = {
		.name = "isp",
		.sta_mask = PWR_STATUS_ISP,
		.ctl_offs = SPM_ISP_PWR_CON,
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(13, 12),
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
	},
	[MT8167_POWER_DOMAIN_MFG_ASYNC] = {
		.name = "mfg_async",
		.sta_mask = MT8167_PWR_STATUS_MFG_ASYNC,
		.ctl_offs = SPM_MFG_ASYNC_PWR_CON,
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = 0,
		.sram_pdn_ack_bits = 0,
		.bp_cfg = {
			BUS_PROT_INFRA_UPDATE_TOPAXI(MT8167_TOP_AXI_PROT_EN_MCU_MFG |
						     MT8167_TOP_AXI_PROT_EN_MFG_EMI),
		},
	},
	[MT8167_POWER_DOMAIN_MFG_2D] = {
		.name = "mfg_2d",
		.sta_mask = MT8167_PWR_STATUS_MFG_2D,
		.ctl_offs = SPM_MFG_2D_PWR_CON,
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
	},
	[MT8167_POWER_DOMAIN_MFG] = {
		.name = "mfg",
		.sta_mask = PWR_STATUS_MFG,
		.ctl_offs = SPM_MFG_PWR_CON,
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),
	},
	[MT8167_POWER_DOMAIN_CONN] = {
		.name = "conn",
		.sta_mask = PWR_STATUS_CONN,
		.ctl_offs = SPM_CONN_PWR_CON,
		.pwr_sta_offs = SPM_PWR_STATUS,
		.pwr_sta2nd_offs = SPM_PWR_STATUS_2ND,
		.sram_pdn_bits = GENMASK(8, 8),
		.sram_pdn_ack_bits = 0,
		.caps = MTK_SCPD_ACTIVE_WAKEUP,
		.bp_cfg = {
			BUS_PROT_INFRA_UPDATE_TOPAXI(MT8167_TOP_AXI_PROT_EN_CONN_EMI |
						     MT8167_TOP_AXI_PROT_EN_CONN_MCU |
						     MT8167_TOP_AXI_PROT_EN_MCU_CONN),
		},
	},
};

static const struct scpsys_soc_data mt8167_scpsys_data = {
	.domains_data = scpsys_domain_data_mt8167,
	.num_domains = ARRAY_SIZE(scpsys_domain_data_mt8167),
};

#endif /* __SOC_MEDIATEK_MT8167_PM_DOMAINS_H */

