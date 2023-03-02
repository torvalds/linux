/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTW8723D_H__
#define __RTW8723D_H__

enum rtw8723d_path {
	PATH_S1,
	PATH_S0,
	PATH_NR,
};

enum rtw8723d_iqk_round {
	IQK_ROUND_0,
	IQK_ROUND_1,
	IQK_ROUND_2,
	IQK_ROUND_HYBRID,
	IQK_ROUND_SIZE,
	IQK_ROUND_INVALID = 0xff,
};

enum rtw8723d_iqk_result {
	IQK_S1_TX_X,
	IQK_S1_TX_Y,
	IQK_S1_RX_X,
	IQK_S1_RX_Y,
	IQK_S0_TX_X,
	IQK_S0_TX_Y,
	IQK_S0_RX_X,
	IQK_S0_RX_Y,
	IQK_NR,
	IQK_SX_NR = IQK_NR / PATH_NR,
};

struct rtw8723de_efuse {
	u8 mac_addr[ETH_ALEN];		/* 0xd0 */
	u8 vender_id[2];
	u8 device_id[2];
	u8 sub_vender_id[2];
	u8 sub_device_id[2];
};

struct rtw8723du_efuse {
	u8 res4[48];                    /* 0xd0 */
	u8 vender_id[2];                /* 0x100 */
	u8 product_id[2];               /* 0x102 */
	u8 usb_option;                  /* 0x104 */
	u8 mac_addr[ETH_ALEN];          /* 0x107 */
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
	union {
		struct rtw8723de_efuse e;
		struct rtw8723du_efuse u;
	};
};

extern const struct rtw_chip_info rtw8723d_hw_spec;

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

static inline s32 iqkxy_to_s32(s32 val)
{
	/* val is Q10.8 */
	return sign_extend32(val, 9);
}

static inline s32 iqk_mult(s32 x, s32 y, s32 *ext)
{
	/* x, y and return value are Q10.8 */
	s32 t;

	t = x * y;
	if (ext)
		*ext = (t >> 7) & 0x1;	/* Q.16 --> Q.9; get LSB of Q.9 */

	return (t >> 8);	/* Q.16 --> Q.8 */
}

#define OFDM_SWING_A(swing)		FIELD_GET(GENMASK(9, 0), swing)
#define OFDM_SWING_B(swing)		FIELD_GET(GENMASK(15, 10), swing)
#define OFDM_SWING_C(swing)		FIELD_GET(GENMASK(21, 16), swing)
#define OFDM_SWING_D(swing)		FIELD_GET(GENMASK(31, 22), swing)
#define RTW_DEF_OFDM_SWING_INDEX	28
#define RTW_DEF_CCK_SWING_INDEX		28

#define MAX_TOLERANCE	5
#define IQK_TX_X_ERR	0x142
#define IQK_TX_Y_ERR	0x42
#define IQK_RX_X_UPPER	0x11a
#define IQK_RX_X_LOWER	0xe6
#define IQK_RX_Y_LMT	0x1a
#define IQK_TX_OK	BIT(0)
#define IQK_RX_OK	BIT(1)
#define PATH_IQK_RETRY	2

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
#define BIT_LCK			BIT(15)

#define REG_GPIO_INTM		0x0048
#define REG_BTG_SEL		0x0067
#define BIT_MASK_BTG_WL		BIT(7)
#define REG_LTECOEX_PATH_CONTROL	0x0070
#define REG_LTECOEX_CTRL	0x07c0
#define REG_LTECOEX_WRITE_DATA	0x07c4
#define REG_LTECOEX_READ_DATA	0x07c8
#define REG_PSDFN		0x0808
#define REG_BB_PWR_SAV1_11N	0x0874
#define REG_ANA_PARAM1		0x0880
#define REG_ANALOG_P4		0x088c
#define REG_PSDRPT		0x08b4
#define REG_FPGA1_RFMOD		0x0900
#define REG_BB_SEL_BTG		0x0948
#define REG_BBRX_DFIR		0x0954
#define BIT_MASK_RXBB_DFIR	GENMASK(27, 24)
#define BIT_RXBB_DFIR_EN	BIT(19)
#define REG_CCK0_SYS		0x0a00
#define BIT_CCK_SIDE_BAND	BIT(4)
#define REG_CCK_ANT_SEL_11N	0x0a04
#define REG_PWRTH		0x0a08
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
#define REG_PWRTH2		0x0aa8
#define REG_CSRATIO		0x0aaa
#define REG_OFDM_FA_HOLDC_11N	0x0c00
#define BIT_MASK_OFDM_FA_KEEP	BIT(31)
#define REG_BB_RX_PATH_11N	0x0c04
#define REG_TRMUX_11N		0x0c08
#define REG_OFDM_FA_RSTC_11N	0x0c0c
#define BIT_MASK_OFDM_FA_RST	BIT(31)
#define REG_A_RXIQI		0x0c14
#define BIT_MASK_RXIQ_S1_X	0x000003FF
#define BIT_MASK_RXIQ_S1_Y1	0x0000FC00
#define BIT_SET_RXIQ_S1_Y1(y)	((y) & 0x3F)
#define REG_OFDM0_RXDSP		0x0c40
#define BIT_MASK_RXDSP		GENMASK(28, 24)
#define BIT_EN_RXDSP		BIT(9)
#define REG_OFDM_0_ECCA_THRESHOLD	0x0c4c
#define BIT_MASK_OFDM0_EXT_A	BIT(31)
#define BIT_MASK_OFDM0_EXT_C	BIT(29)
#define BIT_MASK_OFDM0_EXTS	(BIT(31) | BIT(29) | BIT(28))
#define BIT_SET_OFDM0_EXTS(a, c, d) (((a) << 31) | ((c) << 29) | ((d) << 28))
#define REG_OFDM0_XAAGC1	0x0c50
#define REG_OFDM0_XBAGC1	0x0c58
#define REG_AGCRSSI		0x0c78
#define REG_OFDM_0_XA_TX_IQ_IMBALANCE	0x0c80
#define BIT_MASK_TXIQ_ELM_A	0x03ff
#define BIT_SET_TXIQ_ELM_ACD(a, c, d) (((d) << 22) | (((c) & 0x3F) << 16) |    \
				       ((a) & 0x03ff))
#define BIT_MASK_TXIQ_ELM_C	GENMASK(21, 16)
#define BIT_SET_TXIQ_ELM_C2(c)	((c) & 0x3F)
#define BIT_MASK_TXIQ_ELM_D	GENMASK(31, 22)
#define REG_TXIQK_MATRIXA_LSB2_11N	0x0c94
#define BIT_SET_TXIQ_ELM_C1(c)	(((c) & 0x000003C0) >> 6)
#define REG_RXIQK_MATRIX_LSB_11N	0x0ca0
#define BIT_MASK_RXIQ_S1_Y2	0xF0000000
#define BIT_SET_RXIQ_S1_Y2(y)	(((y) >> 6) & 0xF)
#define REG_TXIQ_AB_S0		0x0cd0
#define BIT_MASK_TXIQ_A_S0	0x000007FE
#define BIT_MASK_TXIQ_A_EXT_S0	BIT(0)
#define BIT_MASK_TXIQ_B_S0	0x0007E000
#define REG_TXIQ_CD_S0		0x0cd4
#define BIT_MASK_TXIQ_C_S0	0x000007FE
#define BIT_MASK_TXIQ_C_EXT_S0	BIT(0)
#define BIT_MASK_TXIQ_D_S0	GENMASK(22, 13)
#define BIT_MASK_TXIQ_D_EXT_S0	BIT(12)
#define REG_RXIQ_AB_S0		0x0cd8
#define BIT_MASK_RXIQ_X_S0	0x000003FF
#define BIT_MASK_RXIQ_Y_S0	0x003FF000
#define REG_OFDM_FA_TYPE1_11N	0x0cf0
#define BIT_MASK_OFDM_FF_CNT	GENMASK(15, 0)
#define BIT_MASK_OFDM_SF_CNT	GENMASK(31, 16)
#define REG_OFDM_FA_RSTD_11N	0x0d00
#define BIT_MASK_OFDM_FA_RST1	BIT(27)
#define BIT_MASK_OFDM_FA_KEEP1	BIT(31)
#define REG_CTX			0x0d03
#define BIT_MASK_CTX_TYPE	GENMASK(6, 4)
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
#define REG_FPGA0_IQK_11N	0x0e28
#define BIT_MASK_IQK_MOD	0xffffff00
#define EN_IQK			0x808000
#define RST_IQK			0x000000
#define REG_TXIQK_TONE_A_11N	0x0e30
#define REG_RXIQK_TONE_A_11N	0x0e34
#define REG_TXIQK_PI_A_11N	0x0e38
#define REG_RXIQK_PI_A_11N	0x0e3c
#define REG_TXIQK_11N		0x0e40
#define BIT_SET_TXIQK_11N(x, y)	(0x80007C00 | ((x) << 16) | (y))
#define REG_RXIQK_11N		0x0e44
#define REG_IQK_AGC_PTS_11N	0x0e48
#define REG_IQK_AGC_RSP_11N	0x0e4c
#define REG_TX_IQK_TONE_B	0x0e50
#define REG_RX_IQK_TONE_B	0x0e54
#define REG_IQK_RES_TX		0x0e94
#define BIT_MASK_RES_TX		GENMASK(25, 16)
#define REG_IQK_RES_TY		0x0e9c
#define BIT_MASK_RES_TY		GENMASK(25, 16)
#define REG_IQK_RES_RX		0x0ea4
#define BIT_MASK_RES_RX		GENMASK(25, 16)
#define REG_IQK_RES_RY		0x0eac
#define BIT_IQK_TX_FAIL		BIT(28)
#define BIT_IQK_RX_FAIL		BIT(27)
#define BIT_IQK_DONE		BIT(26)
#define BIT_MASK_RES_RY		GENMASK(25, 16)
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
