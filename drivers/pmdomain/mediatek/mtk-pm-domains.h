/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOC_MEDIATEK_MTK_PM_DOMAINS_H
#define __SOC_MEDIATEK_MTK_PM_DOMAINS_H

#define MTK_SCPD_ACTIVE_WAKEUP		BIT(0)
#define MTK_SCPD_FWAIT_SRAM		BIT(1)
#define MTK_SCPD_SRAM_ISO		BIT(2)
#define MTK_SCPD_KEEP_DEFAULT_OFF	BIT(3)
#define MTK_SCPD_DOMAIN_SUPPLY		BIT(4)
/* can't set MTK_SCPD_KEEP_DEFAULT_OFF at the same time */
#define MTK_SCPD_ALWAYS_ON		BIT(5)
#define MTK_SCPD_EXT_BUCK_ISO		BIT(6)
#define MTK_SCPD_HAS_INFRA_NAO		BIT(7)
#define MTK_SCPD_STRICT_BUS_PROTECTION	BIT(8)
#define MTK_SCPD_SRAM_PDN_INVERTED	BIT(9)
#define MTK_SCPD_MODEM_PWRSEQ		BIT(10)
#define MTK_SCPD_SKIP_RESET_B		BIT(11)
#define MTK_SCPD_CAPS(_scpd, _x)	((_scpd)->data->caps & (_x))

#define SPM_VDE_PWR_CON			0x0210
#define SPM_MFG_PWR_CON			0x0214
#define SPM_VEN_PWR_CON			0x0230
#define SPM_ISP_PWR_CON			0x0238
#define SPM_DIS_PWR_CON			0x023c
#define SPM_CONN_PWR_CON		0x0280
#define SPM_MD1_PWR_CON			0x0284
#define SPM_VEN2_PWR_CON		0x0298
#define SPM_AUDIO_PWR_CON		0x029c
#define SPM_MFG_2D_PWR_CON		0x02c0
#define SPM_MFG_ASYNC_PWR_CON		0x02c4
#define SPM_USB_PWR_CON			0x02cc

#define SPM_PWR_STATUS			0x060c
#define SPM_PWR_STATUS_2ND		0x0610

#define PWR_STATUS_MD1			BIT(0)
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

#define SPM_MAX_BUS_PROT_DATA		7

enum scpsys_bus_prot_flags {
	BUS_PROT_REG_UPDATE = BIT(1),
	BUS_PROT_IGNORE_CLR_ACK = BIT(2),
	BUS_PROT_INVERTED = BIT(3),
};

enum scpsys_bus_prot_block {
	BUS_PROT_BLOCK_INFRA,
	BUS_PROT_BLOCK_INFRA_NAO,
	BUS_PROT_BLOCK_SMI,
	BUS_PROT_BLOCK_COUNT,
};

#define _BUS_PROT_STA(_hwip, _sta_hwip, _set_clr_mask, _set, _clr,	\
		      _sta_mask, _sta, _flags)				\
	{								\
		.bus_prot_block = BUS_PROT_BLOCK_##_hwip,		\
		.bus_prot_sta_block = BUS_PROT_BLOCK_##_sta_hwip,	\
		.bus_prot_set_clr_mask = (_set_clr_mask),		\
		.bus_prot_set = _set,					\
		.bus_prot_clr = _clr,					\
		.bus_prot_sta_mask = (_sta_mask),			\
		.bus_prot_sta = _sta,					\
		.flags = _flags						\
	}

#define _BUS_PROT(_hwip, _set_clr_mask, _set, _clr, _sta_mask,		\
		  _sta, _flags)						\
	_BUS_PROT_STA(_hwip, _hwip, _set_clr_mask, _set, _clr,		\
		      _sta_mask, _sta, _flags)

#define BUS_PROT_WR(_hwip, _mask, _set, _clr, _sta)			\
		_BUS_PROT(_hwip, _mask, _set, _clr,  _mask, _sta, 0)

#define BUS_PROT_WR_IGN(_hwip, _mask, _set, _clr, _sta)		\
		_BUS_PROT(_hwip, _mask, _set, _clr, _mask, _sta,	\
			  BUS_PROT_IGNORE_CLR_ACK)

#define BUS_PROT_UPDATE(_hwip, _mask, _set, _clr, _sta)			\
		_BUS_PROT(_hwip, _mask, _set, _clr, _mask, _sta,	\
			  BUS_PROT_REG_UPDATE)

#define BUS_PROT_INFRA_UPDATE_TOPAXI(_mask)			\
		BUS_PROT_UPDATE(INFRA, _mask,			\
				INFRA_TOPAXI_PROTECTEN,		\
				INFRA_TOPAXI_PROTECTEN,		\
				INFRA_TOPAXI_PROTECTSTA1)

struct scpsys_bus_prot_data {
	u8 bus_prot_block;
	u8 bus_prot_sta_block;
	u32 bus_prot_set_clr_mask;
	u32 bus_prot_set;
	u32 bus_prot_clr;
	u32 bus_prot_sta_mask;
	u32 bus_prot_sta;
	u8 flags;
};

/**
 * enum scpsys_rtff_type - Type of RTFF Hardware for power domain
 * @SCPSYS_RTFF_NONE:          RTFF HW not present or domain not RTFF managed
 * @SCPSYS_RTFF_TYPE_GENERIC:  Non-CPU, peripheral-generic RTFF HW
 * @SCPSYS_RTFF_TYPE_PCIE_PHY: PCI-Express PHY specific RTFF HW
 * @SCPSYS_RTFF_TYPE_STOR_UFS: Storage (UFS) specific RTFF HW
 * @SCPSYS_RTFF_TYPE_MAX:      Number of supported RTFF HW Types
 */
enum scpsys_rtff_type {
	SCPSYS_RTFF_NONE = 0,
	SCPSYS_RTFF_TYPE_GENERIC,
	SCPSYS_RTFF_TYPE_PCIE_PHY,
	SCPSYS_RTFF_TYPE_STOR_UFS,
	SCPSYS_RTFF_TYPE_MAX
};

/**
 * struct scpsys_domain_data - scp domain data for power on/off flow
 * @name: The name of the power domain.
 * @sta_mask: The mask for power on/off status bit.
 * @ctl_offs: The offset for main power control register.
 * @sram_pdn_bits: The mask for sram power control bits.
 * @sram_pdn_ack_bits: The mask for sram power control acked bits.
 * @ext_buck_iso_offs: The offset for external buck isolation
 * @ext_buck_iso_mask: The mask for external buck isolation
 * @caps: The flag for active wake-up action.
 * @rtff_type: The power domain RTFF HW type
 * @bp_cfg: bus protection configuration for any subsystem
 */
struct scpsys_domain_data {
	const char *name;
	u32 sta_mask;
	int ctl_offs;
	u32 sram_pdn_bits;
	u32 sram_pdn_ack_bits;
	int ext_buck_iso_offs;
	u32 ext_buck_iso_mask;
	u16 caps;
	enum scpsys_rtff_type rtff_type;
	const struct scpsys_bus_prot_data bp_cfg[SPM_MAX_BUS_PROT_DATA];
	int pwr_sta_offs;
	int pwr_sta2nd_offs;
};

struct scpsys_soc_data {
	const struct scpsys_domain_data *domains_data;
	int num_domains;
	enum scpsys_bus_prot_block *bus_prot_blocks;
	int num_bus_prot_blocks;
};

#endif /* __SOC_MEDIATEK_MTK_PM_DOMAINS_H */
