/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTW8822B_H__
#define __RTW8822B_H__

#include <asm/byteorder.h>

#define RCR_VHT_ACK		BIT(26)

struct rtw8822bu_efuse {
	u8 res4[4];			/* 0xd0 */
	u8 usb_optional_function;
	u8 res5[0x1e];
	u8 res6[2];
	u8 serial[0x0b];		/* 0xf5 */
	u8 vid;				/* 0x100 */
	u8 res7;
	u8 pid;
	u8 res8[4];
	u8 mac_addr[ETH_ALEN];		/* 0x107 */
	u8 res9[2];
	u8 vendor_name[0x07];
	u8 res10[2];
	u8 device_name[0x14];
	u8 res11[0xcf];
	u8 package_type;		/* 0x1fb */
	u8 res12[0x4];
};

struct rtw8822be_efuse {
	u8 mac_addr[ETH_ALEN];		/* 0xd0 */
	u8 vender_id[2];
	u8 device_id[2];
	u8 sub_vender_id[2];
	u8 sub_device_id[2];
	u8 pmc[2];
	u8 exp_device_cap[2];
	u8 msi_cap;
	u8 ltr_cap;			/* 0xe3 */
	u8 exp_link_control[2];
	u8 link_cap[4];
	u8 link_control[2];
	u8 serial_number[8];
	u8 res0:2;			/* 0xf4 */
	u8 ltr_en:1;
	u8 res1:2;
	u8 obff:2;
	u8 res2:3;
	u8 obff_cap:2;
	u8 res3:4;
	u8 res4[3];
	u8 class_code[3];
	u8 pci_pm_L1_2_supp:1;
	u8 pci_pm_L1_1_supp:1;
	u8 aspm_pm_L1_2_supp:1;
	u8 aspm_pm_L1_1_supp:1;
	u8 L1_pm_substates_supp:1;
	u8 res5:3;
	u8 port_common_mode_restore_time;
	u8 port_t_power_on_scale:2;
	u8 res6:1;
	u8 port_t_power_on_value:5;
	u8 res7;
};

struct rtw8822b_efuse {
	__le16 rtl_id;
	u8 res0[0x0e];

	/* power index for four RF paths */
	struct rtw_txpwr_idx txpwr_idx_table[4];

	u8 channel_plan;		/* 0xb8 */
	u8 xtal_k;
	u8 thermal_meter;
	u8 iqk_lck;
	u8 pa_type;			/* 0xbc */
	u8 lna_type_2g[2];		/* 0xbd */
	u8 lna_type_5g[2];
	u8 rf_board_option;
	u8 rf_feature_option;
	u8 rf_bt_setting;
	u8 eeprom_version;
	u8 eeprom_customer_id;
	u8 tx_bb_swing_setting_2g;
	u8 tx_bb_swing_setting_5g;
	u8 tx_pwr_calibrate_rate;
	u8 rf_antenna_option;		/* 0xc9 */
	u8 rfe_option;
	u8 country_code[2];
	u8 res[3];
	union {
		struct rtw8822bu_efuse u;
		struct rtw8822be_efuse e;
	};
};

static inline void
_rtw_write32s_mask(struct rtw_dev *rtwdev, u32 addr, u32 mask, u32 data)
{
	/* 0xC00-0xCFF and 0xE00-0xEFF have the same layout */
	rtw_write32_mask(rtwdev, addr, mask, data);
	rtw_write32_mask(rtwdev, addr + 0x200, mask, data);
}

#define rtw_write32s_mask(rtwdev, addr, mask, data)			       \
	do {								       \
		BUILD_BUG_ON((addr) < 0xC00 || (addr) >= 0xD00);	       \
									       \
		_rtw_write32s_mask(rtwdev, addr, mask, data);		       \
	} while (0)

/* phy status page0 */
#define GET_PHY_STAT_P0_PWDB(phy_stat)                                         \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x00), GENMASK(15, 8))

/* phy status page1 */
#define GET_PHY_STAT_P1_PWDB_A(phy_stat)                                       \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x00), GENMASK(15, 8))
#define GET_PHY_STAT_P1_PWDB_B(phy_stat)                                       \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x00), GENMASK(23, 16))
#define GET_PHY_STAT_P1_RF_MODE(phy_stat)                                      \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x03), GENMASK(29, 28))
#define GET_PHY_STAT_P1_L_RXSC(phy_stat)                                       \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x01), GENMASK(11, 8))
#define GET_PHY_STAT_P1_HT_RXSC(phy_stat)                                      \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x01), GENMASK(15, 12))
#define GET_PHY_STAT_P1_RXEVM_A(phy_stat)                                      \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x04), GENMASK(7, 0))
#define GET_PHY_STAT_P1_RXEVM_B(phy_stat)                                      \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x04), GENMASK(15, 8))
#define GET_PHY_STAT_P1_CFO_TAIL_A(phy_stat)                                 \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x05), GENMASK(7, 0))
#define GET_PHY_STAT_P1_CFO_TAIL_B(phy_stat)                                 \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x05), GENMASK(15, 8))
#define GET_PHY_STAT_P1_RXSNR_A(phy_stat)                                      \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x06), GENMASK(7, 0))
#define GET_PHY_STAT_P1_RXSNR_B(phy_stat)                                      \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x06), GENMASK(15, 8))

#define RTW8822B_EDCCA_MAX	0x7f
#define RTW8822B_EDCCA_SRC_DEF	1
#define REG_HTSTFWT	0x800
#define REG_RXPSEL	0x808
#define BIT_RX_PSEL_RST		(BIT(28) | BIT(29))
#define REG_TXPSEL	0x80c
#define REG_RXCCAMSK	0x814
#define REG_CCASEL	0x82c
#define REG_PDMFTH	0x830
#define REG_CCA2ND	0x838
#define REG_L1WT	0x83c
#define REG_L1PKWT	0x840
#define REG_MRC		0x850
#define REG_CLKTRK	0x860
#define REG_EDCCA_POW_MA	0x8a0
#define BIT_MA_LEVEL	GENMASK(1, 0)
#define REG_ADCCLK	0x8ac
#define REG_ADC160	0x8c4
#define REG_ADC40	0x8c8
#define REG_EDCCA_DECISION	0x8dc
#define BIT_EDCCA_OPTION	BIT(5)
#define REG_CDDTXP	0x93c
#define REG_TXPSEL1	0x940
#define REG_EDCCA_SOURCE	0x944
#define BIT_SOURCE_OPTION	GENMASK(29, 28)
#define REG_ACBB0	0x948
#define REG_ACBBRXFIR	0x94c
#define REG_ACGG2TBL	0x958
#define REG_RXSB	0xa00
#define REG_ADCINI	0xa04
#define REG_TXSF2	0xa24
#define REG_TXSF6	0xa28
#define REG_RXDESC	0xa2c
#define REG_ENTXCCK	0xa80
#define REG_AGCTR_A	0xc08
#define REG_TXDFIR	0xc20
#define REG_RXIGI_A	0xc50
#define REG_TRSW	0xca0
#define REG_RFESEL0	0xcb0
#define REG_RFESEL8	0xcb4
#define REG_RFECTL	0xcb8
#define REG_RFEINV	0xcbc
#define REG_AGCTR_B	0xe08
#define REG_RXIGI_B	0xe50
#define REG_ANTWT	0x1904
#define REG_IQKFAILMSK	0x1bf0

extern const struct rtw_chip_info rtw8822b_hw_spec;

#endif
