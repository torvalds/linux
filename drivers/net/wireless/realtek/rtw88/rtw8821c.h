/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTW8821C_H__
#define __RTW8821C_H__

#include <asm/byteorder.h>

#define RCR_VHT_ACK		BIT(26)

struct rtw8821cu_efuse {
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

struct rtw8821ce_efuse {
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

struct rtw8821cs_efuse {
	u8 res4[0x4a];			/* 0xd0 */
	u8 mac_addr[ETH_ALEN];		/* 0x11a */
} __packed;

struct rtw8821c_efuse {
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
		struct rtw8821ce_efuse e;
		struct rtw8821cu_efuse u;
		struct rtw8821cs_efuse s;
	};
};

static inline void
_rtw_write32s_mask(struct rtw_dev *rtwdev, u32 addr, u32 mask, u32 data)
{
	/* 0xC00-0xCFF and 0xE00-0xEFF have the same layout */
	rtw_write32_mask(rtwdev, addr, mask, data);
	rtw_write32_mask(rtwdev, addr + 0x200, mask, data);
}

extern const struct rtw_chip_info rtw8821c_hw_spec;

#define rtw_write32s_mask(rtwdev, addr, mask, data)			       \
	do {								       \
		BUILD_BUG_ON((addr) < 0xC00 || (addr) >= 0xD00);	       \
									       \
		_rtw_write32s_mask(rtwdev, addr, mask, data);		       \
	} while (0)

#define BIT_FEN_PCIEA BIT(6)
#define WLAN_SLOT_TIME		0x09
#define WLAN_PIFS_TIME		0x19
#define WLAN_SIFS_CCK_CONT_TX	0xA
#define WLAN_SIFS_OFDM_CONT_TX	0xE
#define WLAN_SIFS_CCK_TRX	0x10
#define WLAN_SIFS_OFDM_TRX	0x10
#define WLAN_VO_TXOP_LIMIT	0x186
#define WLAN_VI_TXOP_LIMIT	0x3BC
#define WLAN_RDG_NAV		0x05
#define WLAN_TXOP_NAV		0x1B
#define WLAN_CCK_RX_TSF		0x30
#define WLAN_OFDM_RX_TSF	0x30
#define WLAN_TBTT_PROHIBIT	0x04
#define WLAN_TBTT_HOLD_TIME	0x064
#define WLAN_DRV_EARLY_INT	0x04
#define WLAN_BCN_DMA_TIME	0x02

#define WLAN_RX_FILTER0		0x0FFFFFFF
#define WLAN_RX_FILTER2		0xFFFF
#define WLAN_RCR_CFG		0xE400220E
#define WLAN_RXPKT_MAX_SZ	12288
#define WLAN_RXPKT_MAX_SZ_512	(WLAN_RXPKT_MAX_SZ >> 9)

#define WLAN_AMPDU_MAX_TIME		0x70
#define WLAN_RTS_LEN_TH			0xFF
#define WLAN_RTS_TX_TIME_TH		0x08
#define WLAN_MAX_AGG_PKT_LIMIT		0x20
#define WLAN_RTS_MAX_AGG_PKT_LIMIT	0x20
#define FAST_EDCA_VO_TH		0x06
#define FAST_EDCA_VI_TH		0x06
#define FAST_EDCA_BE_TH		0x06
#define FAST_EDCA_BK_TH		0x06
#define WLAN_BAR_RETRY_LIMIT		0x01
#define WLAN_RA_TRY_RATE_AGG_LIMIT	0x08

#define WLAN_TX_FUNC_CFG1		0x30
#define WLAN_TX_FUNC_CFG2		0x30
#define WLAN_MAC_OPT_NORM_FUNC1		0x98
#define WLAN_MAC_OPT_LB_FUNC1		0x80
#define WLAN_MAC_OPT_FUNC2		0xb0810041

#define WLAN_SIFS_CFG	(WLAN_SIFS_CCK_CONT_TX | \
			(WLAN_SIFS_OFDM_CONT_TX << BIT_SHIFT_SIFS_OFDM_CTX) | \
			(WLAN_SIFS_CCK_TRX << BIT_SHIFT_SIFS_CCK_TRX) | \
			(WLAN_SIFS_OFDM_TRX << BIT_SHIFT_SIFS_OFDM_TRX))

#define WLAN_TBTT_TIME	(WLAN_TBTT_PROHIBIT |\
			(WLAN_TBTT_HOLD_TIME << BIT_SHIFT_TBTT_HOLD_TIME_AP))

#define WLAN_NAV_CFG		(WLAN_RDG_NAV | (WLAN_TXOP_NAV << 16))
#define WLAN_RX_TSF_CFG		(WLAN_CCK_RX_TSF | (WLAN_OFDM_RX_TSF) << 8)
#define WLAN_PRE_TXCNT_TIME_TH		0x1E4

/* phy status page0 */
#define GET_PHY_STAT_P0_PWDB(phy_stat)                                         \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x00), GENMASK(15, 8))
#define GET_PHY_STAT_P0_VGA(phy_stat)                                          \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x03), GENMASK(12, 8))
#define GET_PHY_STAT_P0_LNA_L(phy_stat)                                        \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x03), GENMASK(15, 13))
#define GET_PHY_STAT_P0_LNA_H(phy_stat)                                        \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x03), BIT(23))
#define BIT_LNA_H_MASK BIT(3)
#define BIT_LNA_L_MASK GENMASK(2, 0)

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

#define REG_SYS_CTRL	0x000
#define BIT_FEN_EN	BIT(26)
#define REG_INIRTS_RATE_SEL 0x0480
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
#define REG_ADCCLK	0x8ac
#define REG_ADC160	0x8c4
#define REG_ADC40	0x8c8
#define REG_CHFIR	0x8f0
#define REG_CDDTXP	0x93c
#define REG_TXPSEL1	0x940
#define REG_ACBB0	0x948
#define REG_ACBBRXFIR	0x94c
#define REG_ACGG2TBL	0x958
#define REG_FAS		0x9a4
#define REG_RXSB	0xa00
#define REG_ADCINI	0xa04
#define REG_PWRTH	0xa08
#define REG_TXSF2	0xa24
#define REG_TXSF6	0xa28
#define REG_FA_CCK	0xa5c
#define REG_RXDESC	0xa2c
#define REG_ENTXCCK	0xa80
#define BTG_LNA		0xfc84
#define WLG_LNA		0x7532
#define REG_ENRXCCA	0xa84
#define BTG_CCA		0x0e
#define WLG_CCA		0x12
#define REG_PWRTH2	0xaa8
#define REG_CSRATIO	0xaaa
#define REG_TXFILTER	0xaac
#define REG_CNTRST	0xb58
#define REG_AGCTR_A	0xc08
#define REG_TXSCALE_A	0xc1c
#define REG_TXDFIR	0xc20
#define REG_RXIGI_A	0xc50
#define REG_TXAGCIDX	0xc94
#define REG_TRSW	0xca0
#define REG_RFESEL0	0xcb0
#define REG_RFESEL8	0xcb4
#define REG_RFECTL	0xcb8
#define B_BTG_SWITCH	BIT(16)
#define B_CTRL_SWITCH	BIT(18)
#define B_WL_SWITCH	(BIT(20) | BIT(22))
#define B_WLG_SWITCH	BIT(21)
#define B_WLA_SWITCH	BIT(23)
#define REG_RFEINV	0xcbc
#define REG_AGCTR_B	0xe08
#define REG_RXIGI_B	0xe50
#define REG_CRC_CCK	0xf04
#define REG_CRC_OFDM	0xf14
#define REG_CRC_HT	0xf10
#define REG_CRC_VHT	0xf0c
#define REG_CCA_OFDM	0xf08
#define REG_FA_OFDM	0xf48
#define REG_CCA_CCK	0xfcc
#define REG_DMEM_CTRL	0x1080
#define BIT_WL_RST	BIT(16)
#define REG_ANTWT	0x1904
#define REG_IQKFAILMSK	0x1bf0
#define BIT_MASK_R_RFE_SEL_15	GENMASK(31, 28)
#define BIT_SDIO_INT BIT(18)
#define BT_CNT_ENABLE	0x1
#define BIT_BCN_QUEUE	BIT(3)
#define BCN_PRI_EN	0x1
#define PTA_CTRL_PIN	0x66
#define DPDT_CTRL_PIN	0x77
#define ANTDIC_CTRL_PIN	0x88
#define REG_CTRL_TYPE	0x67
#define BIT_CTRL_TYPE1	BIT(5)
#define BIT_CTRL_TYPE2	BIT(4)
#define CTRL_TYPE_MASK	GENMASK(15, 8)

#define RF18_BAND_MASK		(BIT(16) | BIT(9) | BIT(8))
#define RF18_BAND_2G		(0)
#define RF18_BAND_5G		(BIT(16) | BIT(8))
#define RF18_CHANNEL_MASK	(MASKBYTE0)
#define RF18_RFSI_MASK		(BIT(18) | BIT(17))
#define RF18_RFSI_GE		(BIT(17))
#define RF18_RFSI_GT		(BIT(18))
#define RF18_BW_MASK		(BIT(11) | BIT(10))
#define RF18_BW_20M		(BIT(11) | BIT(10))
#define RF18_BW_40M		(BIT(11))
#define RF18_BW_80M		(BIT(10))

#endif
