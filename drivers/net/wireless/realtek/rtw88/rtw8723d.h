/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTW8723D_H__
#define __RTW8723D_H__

struct rtw8723de_efuse {
	u8 mac_addr[ETH_ALEN];		/* 0xd0 */
	u8 vender_id[2];
	u8 device_id[2];
	u8 sub_vender_id[2];
	u8 sub_device_id[2];
};

struct rtw8723d_efuse {
	__le16 rtl_id;
	u8 rsvd[2];
	u8 afe;
	u8 rsvd1[11];

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
	u8 res_c7;
	u8 tx_pwr_calibrate_rate;
	u8 rf_antenna_option;		/* 0xc9 */
	u8 rfe_option;
	u8 country_code[2];
	u8 res[3];
	struct rtw8723de_efuse e;
};

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
#define GET_PHY_STAT_P1_CFO_TAIL_A(phy_stat)                                   \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x05), GENMASK(7, 0))
#define GET_PHY_STAT_P1_RXSNR_A(phy_stat)                                      \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x06), GENMASK(7, 0))

#define SPUR_THRES		0x16
#define CCK_DFIR_NR		3
#define DIS_3WIRE		0xccf000c0
#define EN_3WIRE		0xccc000c0
#define START_PSD		0x400000
#define FREQ_CH13		0xfccd
#define FREQ_CH14		0xff9a
#define RFCFGCH_CHANNEL_MASK	GENMASK(7, 0)
#define RFCFGCH_BW_MASK		(BIT(11) | BIT(10))
#define RFCFGCH_BW_20M		(BIT(11) | BIT(10))
#define RFCFGCH_BW_40M		BIT(10)
#define BIT_MASK_RFMOD		BIT(0)

#define REG_PSDFN		0x0808
#define REG_ANALOG_P4		0x088c
#define REG_PSDRPT		0x08b4
#define REG_FPGA1_RFMOD		0x0900
#define REG_BBRX_DFIR		0x0954
#define BIT_MASK_RXBB_DFIR	GENMASK(27, 24)
#define BIT_RXBB_DFIR_EN	BIT(19)
#define REG_CCK0_SYS		0x0a00
#define BIT_CCK_SIDE_BAND	BIT(4)
#define REG_CCK_FA_RST_11N	0x0a2c
#define BIT_MASK_CCK_CNT_KEEP	BIT(12)
#define BIT_MASK_CCK_CNT_EN	BIT(13)
#define BIT_MASK_CCK_CNT_KPEN	(BIT_MASK_CCK_CNT_KEEP | BIT_MASK_CCK_CNT_EN)
#define BIT_MASK_CCK_FA_KEEP	BIT(14)
#define BIT_MASK_CCK_FA_EN	BIT(15)
#define BIT_MASK_CCK_FA_KPEN	(BIT_MASK_CCK_FA_KEEP | BIT_MASK_CCK_FA_EN)
#define REG_CCK_FA_LSB_11N	0x0a5c
#define REG_CCK_FA_MSB_11N	0x0a58
#define REG_CCK_CCA_CNT_11N	0x0a60
#define BIT_MASK_CCK_FA_MSB	GENMASK(7, 0)
#define BIT_MASK_CCK_FA_LSB	GENMASK(15, 8)
#define REG_OFDM_FA_HOLDC_11N	0x0c00
#define BIT_MASK_OFDM_FA_KEEP	BIT(31)
#define REG_OFDM_FA_RSTC_11N	0x0c0c
#define BIT_MASK_OFDM_FA_RST	BIT(31)
#define REG_OFDM0_RXDSP		0x0c40
#define BIT_MASK_RXDSP		GENMASK(28, 24)
#define BIT_EN_RXDSP		BIT(9)
#define REG_OFDM0_XAAGC1	0x0c50
#define REG_OFDM0_XBAGC1	0x0c58
#define REG_OFDM_FA_TYPE1_11N	0x0cf0
#define BIT_MASK_OFDM_FF_CNT	GENMASK(15, 0)
#define BIT_MASK_OFDM_SF_CNT	GENMASK(31, 16)
#define REG_OFDM_FA_RSTD_11N	0x0d00
#define BIT_MASK_OFDM_FA_RST1	BIT(27)
#define BIT_MASK_OFDM_FA_KEEP1	BIT(31)
#define REG_OFDM1_CFOTRK	0x0d2c
#define BIT_EN_CFOTRK		BIT(28)
#define REG_OFDM1_CSI1		0x0d40
#define REG_OFDM1_CSI2		0x0d44
#define REG_OFDM1_CSI3		0x0d48
#define REG_OFDM1_CSI4		0x0d4c
#define REG_OFDM_FA_TYPE2_11N	0x0da0
#define BIT_MASK_OFDM_CCA_CNT	GENMASK(15, 0)
#define BIT_MASK_OFDM_PF_CNT	GENMASK(31, 16)
#define REG_OFDM_FA_TYPE3_11N	0x0da4
#define BIT_MASK_OFDM_RI_CNT	GENMASK(15, 0)
#define BIT_MASK_OFDM_CRC_CNT	GENMASK(31, 16)
#define REG_OFDM_FA_TYPE4_11N	0x0da8
#define BIT_MASK_OFDM_MNS_CNT	GENMASK(15, 0)
#define REG_PAGE_F_RST_11N		0x0f14
#define BIT_MASK_F_RST_ALL		BIT(16)
#define REG_IGI_C_11N			0x0f84
#define REG_IGI_D_11N			0x0f88
#define REG_HT_CRC32_CNT_11N		0x0f90
#define BIT_MASK_HT_CRC_OK		GENMASK(15, 0)
#define BIT_MASK_HT_CRC_ERR		GENMASK(31, 16)
#define REG_OFDM_CRC32_CNT_11N		0x0f94
#define BIT_MASK_OFDM_LCRC_OK		GENMASK(15, 0)
#define BIT_MASK_OFDM_LCRC_ERR		GENMASK(31, 16)
#define REG_HT_CRC32_CNT_11N_AGG	0x0fb8

#endif
