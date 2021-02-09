/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTW8822C_H__
#define __RTW8822C_H__

#include <asm/byteorder.h>

struct rtw8822cu_efuse {
	u8 res0[0x30];			/* 0x120 */
	u8 vid[2];			/* 0x150 */
	u8 pid[2];
	u8 res1[3];
	u8 mac_addr[ETH_ALEN];		/* 0x157 */
	u8 res2[0x3d];
};

struct rtw8822ce_efuse {
	u8 mac_addr[ETH_ALEN];		/* 0x120 */
	u8 vender_id[2];
	u8 device_id[2];
	u8 sub_vender_id[2];
	u8 sub_device_id[2];
	u8 pmc[2];
	u8 exp_device_cap[2];
	u8 msi_cap;
	u8 ltr_cap;			/* 0x133 */
	u8 exp_link_control[2];
	u8 link_cap[4];
	u8 link_control[2];
	u8 serial_number[8];
	u8 res0:2;			/* 0x144 */
	u8 ltr_en:1;
	u8 res1:2;
	u8 obff:2;
	u8 res2:3;
	u8 obff_cap:2;
	u8 res3:4;
	u8 class_code[3];
	u8 res4;
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

struct rtw8822c_efuse {
	__le16 rtl_id;
	u8 res0[0x0e];

	/* power index for four RF paths */
	struct rtw_txpwr_idx txpwr_idx_table[4];

	u8 channel_plan;		/* 0xb8 */
	u8 xtal_k;
	u8 res1;
	u8 iqk_lck;
	u8 res2[5];			/* 0xbc */
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
	u8 res3[3];
	u8 path_a_thermal;		/* 0xd0 */
	u8 path_b_thermal;
	u8 res4[2];
	u8 rx_gain_gap_2g_ofdm;
	u8 res5;
	u8 rx_gain_gap_2g_cck;
	u8 res6;
	u8 rx_gain_gap_5gl;
	u8 res7;
	u8 rx_gain_gap_5gm;
	u8 res8;
	u8 rx_gain_gap_5gh;
	u8 res9;
	u8 res10[0x42];
	union {
		struct rtw8822cu_efuse u;
		struct rtw8822ce_efuse e;
	};
};

enum rtw8822c_dpk_agc_phase {
	RTW_DPK_GAIN_CHECK,
	RTW_DPK_GAIN_LARGE,
	RTW_DPK_GAIN_LESS,
	RTW_DPK_GL_LARGE,
	RTW_DPK_GL_LESS,
	RTW_DPK_LOSS_CHECK,
	RTW_DPK_AGC_OUT,
};

enum rtw8822c_dpk_one_shot_action {
	RTW_DPK_CAL_PWR,
	RTW_DPK_GAIN_LOSS,
	RTW_DPK_DO_DPK,
	RTW_DPK_DPK_ON,
	RTW_DPK_DAGC,
	RTW_DPK_ACTION_MAX
};

void rtw8822c_parse_tbl_dpk(struct rtw_dev *rtwdev,
			    const struct rtw_table *tbl);

#define RTW_DECL_TABLE_DPK(name)			\
const struct rtw_table name ## _tbl = {			\
	.data = name,					\
	.size = ARRAY_SIZE(name),			\
	.parse = rtw8822c_parse_tbl_dpk,		\
}

#define DACK_PATH_8822C		2
#define DACK_REG_8822C		16
#define DACK_RF_8822C		1
#define DACK_SN_8822C		100

/* phy status page0 */
#define GET_PHY_STAT_P0_PWDB_A(phy_stat)                                       \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x00), GENMASK(15, 8))
#define GET_PHY_STAT_P0_PWDB_B(phy_stat)                                       \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x04), GENMASK(7, 0))
#define GET_PHY_STAT_P0_GAIN_A(phy_stat)                                       \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x00), GENMASK(21, 16))
#define GET_PHY_STAT_P0_GAIN_B(phy_stat)                                       \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x04), GENMASK(29, 24))

/* phy status page1 */
#define GET_PHY_STAT_P1_PWDB_A(phy_stat)                                       \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x00), GENMASK(15, 8))
#define GET_PHY_STAT_P1_PWDB_B(phy_stat)                                       \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x00), GENMASK(23, 16))
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

#define REG_ANAPARLDO_POW_MAC	0x0029
#define BIT_LDOE25_PON		BIT(0)

#define REG_TXDFIR0	0x808
#define REG_DFIRBW	0x810
#define REG_ANTMAP0	0x820
#define REG_ANTMAP	0x824
#define REG_DYMPRITH	0x86c
#define REG_DYMENTH0	0x870
#define REG_DYMENTH	0x874
#define REG_SBD		0x88c
#define BITS_SUBTUNE		GENMASK(15, 12)
#define REG_DYMTHMIN	0x8a4
#define REG_TXBWCTL	0x9b0
#define REG_TXCLK	0x9b4
#define REG_SCOTRK	0xc30
#define REG_MRCM	0xc38
#define REG_AGCSWSH	0xc44
#define REG_ANTWTPD	0xc54
#define REG_PT_CHSMO	0xcbc
#define BIT_PT_OPT		BIT(21)
#define REG_ORITXCODE	0x1800
#define REG_3WIRE	0x180c
#define BIT_3WIRE_TX_EN		BIT(0)
#define BIT_3WIRE_RX_EN		BIT(1)
#define BIT_3WIRE_PI_ON		BIT(28)
#define REG_ANAPAR_A	0x1830
#define BIT_ANAPAR_UPDATE	BIT(29)
#define REG_RXAGCCTL0	0x18ac
#define BITS_RXAGC_CCK		GENMASK(15, 12)
#define BITS_RXAGC_OFDM		GENMASK(8, 4)
#define REG_DCKA_I_0	0x18bc
#define REG_DCKA_I_1	0x18c0
#define REG_DCKA_Q_0	0x18d8
#define REG_DCKA_Q_1	0x18dc
#define REG_CCKSB	0x1a00
#define REG_RXCCKSEL	0x1a04
#define REG_BGCTRL	0x1a14
#define BITS_RX_IQ_WEIGHT	(BIT(8) | BIT(9))
#define REG_TXF0	0x1a20
#define REG_TXF1	0x1a24
#define REG_TXF2	0x1a28
#define REG_CCANRX	0x1a2c
#define BIT_CCK_FA_RST		(BIT(14) | BIT(15))
#define BIT_OFDM_FA_RST		(BIT(12) | BIT(13))
#define REG_CCK_FACNT	0x1a5c
#define REG_CCKTXONLY	0x1a80
#define BIT_BB_CCK_CHECK_EN	BIT(18)
#define REG_TXF3	0x1a98
#define REG_TXF4	0x1a9c
#define REG_TXF5	0x1aa0
#define REG_TXF6	0x1aac
#define REG_TXF7	0x1ab0
#define REG_CCK_SOURCE	0x1abc
#define BIT_NBI_EN		BIT(30)
#define REG_IQKSTAT	0x1b10
#define REG_TXANT	0x1c28
#define REG_ENCCK	0x1c3c
#define BIT_CCK_BLK_EN		BIT(1)
#define BIT_CCK_OFDM_BLK_EN	(BIT(0) | BIT(1))
#define REG_CCAMSK	0x1c80
#define REG_RSTB	0x1c90
#define BIT_RSTB_3WIRE		BIT(8)
#define REG_RX_BREAK	0x1d2c
#define BIT_COM_RX_GCK_EN	BIT(31)
#define REG_RXFNCTL	0x1d30
#define REG_RXIGI	0x1d70
#define REG_ENFN	0x1e24
#define REG_TXANTSEG	0x1e28
#define REG_TXLGMAP	0x1e2c
#define REG_CCKPATH	0x1e5c
#define REG_CNT_CTRL	0x1eb4
#define BIT_ALL_CNT_RST		BIT(25)
#define REG_OFDM_FACNT	0x2d00
#define REG_OFDM_FACNT1	0x2d04
#define REG_OFDM_FACNT2	0x2d08
#define REG_OFDM_FACNT3	0x2d0c
#define REG_OFDM_FACNT4	0x2d10
#define REG_OFDM_FACNT5	0x2d20
#define REG_RPT_CIP	0x2d9c
#define REG_OFDM_TXCNT	0x2de0
#define REG_ORITXCODE2	0x4100
#define REG_3WIRE2	0x410c
#define REG_ANAPAR_B	0x4130
#define REG_RXAGCCTL	0x41ac
#define REG_DCKB_I_0	0x41bc
#define REG_DCKB_I_1	0x41c0
#define REG_DCKB_Q_0	0x41d8
#define REG_DCKB_Q_1	0x41dc

#define RF_MODE_TRXAGC		0x00
#define RF_RXAGC_OFFSET		0x19
#define RF_BW_TRXBB		0x1a
#define RF_TX_GAIN_OFFSET	0x55
#define RF_TX_GAIN		0x56
#define RF_TXA_LB_SW		0x63
#define RF_RXG_GAIN		0x87
#define RF_RXA_MIX_GAIN		0x8a
#define RF_EXT_TIA_BW		0x8f
#define RF_DEBUG		0xde

#define REG_NCTL0		0x1b00
#define REG_DPD_CTL0_S0		0x1b04
#define REG_DPD_CTL1_S0		0x1b08
#define REG_IQK_CTL1		0x1b20
#define REG_DPD_LUT0		0x1b44
#define REG_DPD_CTL0_S1		0x1b5c
#define REG_DPD_LUT3		0x1b60
#define REG_DPD_CTL1_S1		0x1b60
#define REG_DPD_AGC		0x1b67
#define REG_DPD_CTL0		0x1bb4
#define REG_R_CONFIG		0x1bcc
#define REG_RXSRAM_CTL		0x1bd4
#define REG_DPD_CTL11		0x1be4
#define REG_DPD_CTL12		0x1be8
#define REG_DPD_CTL15		0x1bf4
#define REG_DPD_CTL16		0x1bf8
#define REG_STAT_RPT		0x1bfc

#define BIT_EXT_TIA_BW		BIT(1)
#define BIT_DE_TRXBW		BIT(2)
#define BIT_DE_TX_GAIN		BIT(16)
#define BIT_RXG_GAIN		BIT(18)
#define BIT_DE_PWR_TRIM		BIT(19)
#define BIT_INNER_LB		BIT(21)
#define BIT_BYPASS_DPD		BIT(25)
#define BIT_DPD_EN		BIT(31)
#define BIT_SUBPAGE		GENMASK(3, 0)
#define BIT_TXAGC		GENMASK(4, 0)
#define BIT_GAIN_TXBB		GENMASK(4, 0)
#define BIT_LB_ATT		GENMASK(4, 2)
#define BIT_RXA_MIX_GAIN	GENMASK(4, 3)
#define BIT_IQ_SWITCH		GENMASK(5, 0)
#define BIT_DPD_CLK		GENMASK(7, 4)
#define BIT_RXAGC		GENMASK(9, 5)
#define BIT_BW_RXBB		GENMASK(11, 10)
#define BIT_LB_SW		GENMASK(13, 12)
#define BIT_BW_TXBB		GENMASK(14, 12)
#define BIT_GLOSS_DB		GENMASK(14, 12)
#define BIT_TXA_LB_ATT		GENMASK(15, 14)
#define BIT_TX_OFFSET_VAL	GENMASK(18, 14)
#define BIT_RPT_SEL		GENMASK(20, 16)
#define BIT_GS_PWSF		GENMASK(27, 0)
#define BIT_RPT_DGAIN		GENMASK(27, 16)
#define BIT_TX_CFIR		GENMASK(31, 30)

#define PPG_THERMAL_A 0x1ef
#define PPG_THERMAL_B 0x1b0
#define RF_THEMAL_MASK GENMASK(19, 16)
#define PPG_2GL_TXAB 0x1d4
#define PPG_2GM_TXAB 0x1ee
#define PPG_2GH_TXAB 0x1d2
#define PPG_2G_A_MASK GENMASK(3, 0)
#define PPG_2G_B_MASK GENMASK(7, 4)
#define PPG_5GL1_TXA 0x1ec
#define PPG_5GL2_TXA 0x1e8
#define PPG_5GM1_TXA 0x1e4
#define PPG_5GM2_TXA 0x1e0
#define PPG_5GH1_TXA 0x1dc
#define PPG_5GL1_TXB 0x1eb
#define PPG_5GL2_TXB 0x1e7
#define PPG_5GM1_TXB 0x1e3
#define PPG_5GM2_TXB 0x1df
#define PPG_5GH1_TXB 0x1db
#define PPG_5G_MASK GENMASK(4, 0)
#define PPG_PABIAS_2GA 0x1d6
#define PPG_PABIAS_2GB 0x1d5
#define PPG_PABIAS_5GA 0x1d8
#define PPG_PABIAS_5GB 0x1d7
#define PPG_PABIAS_MASK GENMASK(3, 0)
#define RF_PABIAS_2G_MASK GENMASK(15, 12)
#define RF_PABIAS_5G_MASK GENMASK(19, 16)

#endif
