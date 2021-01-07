/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOC_MEDIATEK_MTK_PM_DOMAINS_H
#define __SOC_MEDIATEK_MTK_PM_DOMAINS_H

#define MTK_SCPD_ACTIVE_WAKEUP		BIT(0)
#define MTK_SCPD_FWAIT_SRAM		BIT(1)
#define MTK_SCPD_SRAM_ISO		BIT(2)
#define MTK_SCPD_KEEP_DEFAULT_OFF	BIT(3)
#define MTK_SCPD_CAPS(_scpd, _x)	((_scpd)->data->caps & (_x))

#define SPM_VDE_PWR_CON			0x0210
#define SPM_MFG_PWR_CON			0x0214
#define SPM_VEN_PWR_CON			0x0230
#define SPM_ISP_PWR_CON			0x0238
#define SPM_DIS_PWR_CON			0x023c
#define SPM_VEN2_PWR_CON		0x0298
#define SPM_AUDIO_PWR_CON		0x029c
#define SPM_MFG_2D_PWR_CON		0x02c0
#define SPM_MFG_ASYNC_PWR_CON		0x02c4
#define SPM_USB_PWR_CON			0x02cc

#define SPM_PWR_STATUS			0x060c
#define SPM_PWR_STATUS_2ND		0x0610

#define PWR_STATUS_CONN			BIT(1)
#define PWR_STATUS_DISP			BIT(3)
#define PWR_STATUS_MFG			BIT(4)
#define PWR_STATUS_ISP			BIT(5)
#define PWR_STATUS_VDEC			BIT(7)
#define PWR_STATUS_VENC_LT		BIT(20)
#define PWR_STATUS_VENC			BIT(21)
#define PWR_STATUS_MFG_2D		BIT(22)
#define PWR_STATUS_MFG_ASYNC		BIT(23)
#define PWR_STATUS_AUDIO		BIT(24)
#define PWR_STATUS_USB			BIT(25)

#define SPM_MAX_BUS_PROT_DATA		5

#define _BUS_PROT(_mask, _set, _clr, _sta, _update, _ignore) {	\
		.bus_prot_mask = (_mask),			\
		.bus_prot_set = _set,				\
		.bus_prot_clr = _clr,				\
		.bus_prot_sta = _sta,				\
		.bus_prot_reg_update = _update,			\
		.ignore_clr_ack = _ignore,			\
	}

#define BUS_PROT_WR(_mask, _set, _clr, _sta)			\
		_BUS_PROT(_mask, _set, _clr, _sta, false, false)

#define BUS_PROT_WR_IGN(_mask, _set, _clr, _sta)		\
		_BUS_PROT(_mask, _set, _clr, _sta, false, true)

#define BUS_PROT_UPDATE(_mask, _set, _clr, _sta)		\
		_BUS_PROT(_mask, _set, _clr, _sta, true, false)

#define BUS_PROT_UPDATE_TOPAXI(_mask)				\
		BUS_PROT_UPDATE(_mask,				\
				INFRA_TOPAXI_PROTECTEN,		\
				INFRA_TOPAXI_PROTECTEN_CLR,	\
				INFRA_TOPAXI_PROTECTSTA1)

struct scpsys_bus_prot_data {
	u32 bus_prot_mask;
	u32 bus_prot_set;
	u32 bus_prot_clr;
	u32 bus_prot_sta;
	bool bus_prot_reg_update;
	bool ignore_clr_ack;
};

#define MAX_SUBSYS_CLKS 10

/**
 * struct scpsys_domain_data - scp domain data for power on/off flow
 * @sta_mask: The mask for power on/off status bit.
 * @ctl_offs: The offset for main power control register.
 * @sram_pdn_bits: The mask for sram power control bits.
 * @sram_pdn_ack_bits: The mask for sram power control acked bits.
 * @caps: The flag for active wake-up action.
 * @bp_infracfg: bus protection for infracfg subsystem
 * @bp_smi: bus protection for smi subsystem
 */
struct scpsys_domain_data {
	u32 sta_mask;
	int ctl_offs;
	u32 sram_pdn_bits;
	u32 sram_pdn_ack_bits;
	u8 caps;
	const struct scpsys_bus_prot_data bp_infracfg[SPM_MAX_BUS_PROT_DATA];
	const struct scpsys_bus_prot_data bp_smi[SPM_MAX_BUS_PROT_DATA];
};

struct scpsys_soc_data {
	const struct scpsys_domain_data *domains_data;
	int num_domains;
	int pwr_sta_offs;
	int pwr_sta2nd_offs;
};

#endif /* __SOC_MEDIATEK_MTK_PM_DOMAINS_H */
