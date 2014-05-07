/******************************************************************************
 *
 * Copyright(c) 2009-2014  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef	__RTL8723BE_DM_H__
#define __RTL8723BE_DM_H__

#define	MAIN_ANT		0
#define	AUX_ANT			1
#define	MAIN_ANT_CG_TRX		1
#define	AUX_ANT_CG_TRX		0
#define	MAIN_ANT_CGCS_RX	0
#define	AUX_ANT_CGCS_RX		1

#define	TXSCALE_TABLE_SIZE	30

/*RF REG LIST*/
#define	DM_REG_RF_MODE_11N			0x00
#define	DM_REG_RF_0B_11N			0x0B
#define	DM_REG_CHNBW_11N			0x18
#define	DM_REG_T_METER_11N			0x24
#define	DM_REG_RF_25_11N			0x25
#define	DM_REG_RF_26_11N			0x26
#define	DM_REG_RF_27_11N			0x27
#define	DM_REG_RF_2B_11N			0x2B
#define	DM_REG_RF_2C_11N			0x2C
#define	DM_REG_RXRF_A3_11N			0x3C
#define	DM_REG_T_METER_92D_11N			0x42
#define	DM_REG_T_METER_88E_11N			0x42

/*BB REG LIST*/
/*PAGE 8 */
#define	DM_REG_BB_CTRL_11N			0x800
#define	DM_REG_RF_PIN_11N			0x804
#define	DM_REG_PSD_CTRL_11N			0x808
#define	DM_REG_TX_ANT_CTRL_11N			0x80C
#define	DM_REG_BB_PWR_SAV5_11N			0x818
#define	DM_REG_CCK_RPT_FORMAT_11N		0x824
#define	DM_REG_RX_DEFUALT_A_11N			0x858
#define	DM_REG_RX_DEFUALT_B_11N			0x85A
#define	DM_REG_BB_PWR_SAV3_11N			0x85C
#define	DM_REG_ANTSEL_CTRL_11N			0x860
#define	DM_REG_RX_ANT_CTRL_11N			0x864
#define	DM_REG_PIN_CTRL_11N			0x870
#define	DM_REG_BB_PWR_SAV1_11N			0x874
#define	DM_REG_ANTSEL_PATH_11N			0x878
#define	DM_REG_BB_3WIRE_11N			0x88C
#define	DM_REG_SC_CNT_11N			0x8C4
#define	DM_REG_PSD_DATA_11N			0x8B4
/*PAGE 9*/
#define	DM_REG_ANT_MAPPING1_11N			0x914
#define	DM_REG_ANT_MAPPING2_11N			0x918
/*PAGE A*/
#define	DM_REG_CCK_ANTDIV_PARA1_11N		0xA00
#define	DM_REG_CCK_CCA_11N			0xA0A
#define	DM_REG_CCK_ANTDIV_PARA2_11N		0xA0C
#define	DM_REG_CCK_ANTDIV_PARA3_11N		0xA10
#define	DM_REG_CCK_ANTDIV_PARA4_11N		0xA14
#define	DM_REG_CCK_FILTER_PARA1_11N		0xA22
#define	DM_REG_CCK_FILTER_PARA2_11N		0xA23
#define	DM_REG_CCK_FILTER_PARA3_11N		0xA24
#define	DM_REG_CCK_FILTER_PARA4_11N		0xA25
#define	DM_REG_CCK_FILTER_PARA5_11N		0xA26
#define	DM_REG_CCK_FILTER_PARA6_11N		0xA27
#define	DM_REG_CCK_FILTER_PARA7_11N		0xA28
#define	DM_REG_CCK_FILTER_PARA8_11N		0xA29
#define	DM_REG_CCK_FA_RST_11N			0xA2C
#define	DM_REG_CCK_FA_MSB_11N			0xA58
#define	DM_REG_CCK_FA_LSB_11N			0xA5C
#define	DM_REG_CCK_CCA_CNT_11N			0xA60
#define	DM_REG_BB_PWR_SAV4_11N			0xA74
/*PAGE B */
#define	DM_REG_LNA_SWITCH_11N			0xB2C
#define	DM_REG_PATH_SWITCH_11N			0xB30
#define	DM_REG_RSSI_CTRL_11N			0xB38
#define	DM_REG_CONFIG_ANTA_11N			0xB68
#define	DM_REG_RSSI_BT_11N			0xB9C
/*PAGE C */
#define	DM_REG_OFDM_FA_HOLDC_11N		0xC00
#define	DM_REG_RX_PATH_11N			0xC04
#define	DM_REG_TRMUX_11N			0xC08
#define	DM_REG_OFDM_FA_RSTC_11N			0xC0C
#define	DM_REG_RXIQI_MATRIX_11N			0xC14
#define	DM_REG_TXIQK_MATRIX_LSB1_11N		0xC4C
#define	DM_REG_IGI_A_11N			0xC50
#define	DM_REG_ANTDIV_PARA2_11N			0xC54
#define	DM_REG_IGI_B_11N			0xC58
#define	DM_REG_ANTDIV_PARA3_11N			0xC5C
#define	DM_REG_BB_PWR_SAV2_11N			0xC70
#define	DM_REG_RX_OFF_11N			0xC7C
#define	DM_REG_TXIQK_MATRIXA_11N		0xC80
#define	DM_REG_TXIQK_MATRIXB_11N		0xC88
#define	DM_REG_TXIQK_MATRIXA_LSB2_11N		0xC94
#define	DM_REG_TXIQK_MATRIXB_LSB2_11N		0xC9C
#define	DM_REG_RXIQK_MATRIX_LSB_11N		0xCA0
#define	DM_REG_ANTDIV_PARA1_11N			0xCA4
#define	DM_REG_OFDM_FA_TYPE1_11N		0xCF0
/*PAGE D */
#define	DM_REG_OFDM_FA_RSTD_11N			0xD00
#define	DM_REG_OFDM_FA_TYPE2_11N		0xDA0
#define	DM_REG_OFDM_FA_TYPE3_11N		0xDA4
#define	DM_REG_OFDM_FA_TYPE4_11N		0xDA8
/*PAGE E */
#define	DM_REG_TXAGC_A_6_18_11N			0xE00
#define	DM_REG_TXAGC_A_24_54_11N		0xE04
#define	DM_REG_TXAGC_A_1_MCS32_11N		0xE08
#define	DM_REG_TXAGC_A_MCS0_3_11N		0xE10
#define	DM_REG_TXAGC_A_MCS4_7_11N		0xE14
#define	DM_REG_TXAGC_A_MCS8_11_11N		0xE18
#define	DM_REG_TXAGC_A_MCS12_15_11N		0xE1C
#define	DM_REG_FPGA0_IQK_11N			0xE28
#define	DM_REG_TXIQK_TONE_A_11N			0xE30
#define	DM_REG_RXIQK_TONE_A_11N			0xE34
#define	DM_REG_TXIQK_PI_A_11N			0xE38
#define	DM_REG_RXIQK_PI_A_11N			0xE3C
#define	DM_REG_TXIQK_11N			0xE40
#define	DM_REG_RXIQK_11N			0xE44
#define	DM_REG_IQK_AGC_PTS_11N			0xE48
#define	DM_REG_IQK_AGC_RSP_11N			0xE4C
#define	DM_REG_BLUETOOTH_11N			0xE6C
#define	DM_REG_RX_WAIT_CCA_11N			0xE70
#define	DM_REG_TX_CCK_RFON_11N			0xE74
#define	DM_REG_TX_CCK_BBON_11N			0xE78
#define	DM_REG_OFDM_RFON_11N			0xE7C
#define	DM_REG_OFDM_BBON_11N			0xE80
#define DM_REG_TX2RX_11N			0xE84
#define	DM_REG_TX2TX_11N			0xE88
#define	DM_REG_RX_CCK_11N			0xE8C
#define	DM_REG_RX_OFDM_11N			0xED0
#define	DM_REG_RX_WAIT_RIFS_11N			0xED4
#define	DM_REG_RX2RX_11N			0xED8
#define	DM_REG_STANDBY_11N			0xEDC
#define	DM_REG_SLEEP_11N			0xEE0
#define	DM_REG_PMPD_ANAEN_11N			0xEEC

/*MAC REG LIST*/
#define	DM_REG_BB_RST_11N			0x02
#define	DM_REG_ANTSEL_PIN_11N			0x4C
#define	DM_REG_EARLY_MODE_11N			0x4D0
#define	DM_REG_RSSI_MONITOR_11N			0x4FE
#define	DM_REG_EDCA_VO_11N			0x500
#define	DM_REG_EDCA_VI_11N			0x504
#define	DM_REG_EDCA_BE_11N			0x508
#define	DM_REG_EDCA_BK_11N			0x50C
#define	DM_REG_TXPAUSE_11N			0x522
#define	DM_REG_RESP_TX_11N			0x6D8
#define	DM_REG_ANT_TRAIN_PARA1_11N		0x7b0
#define	DM_REG_ANT_TRAIN_PARA2_11N		0x7b4

/*DIG Related*/
#define	DM_BIT_IGI_11N				0x0000007F

#define HAL_DM_DIG_DISABLE			BIT(0)
#define HAL_DM_HIPWR_DISABLE			BIT(1)

#define OFDM_TABLE_LENGTH			43
#define CCK_TABLE_LENGTH			33

#define OFDM_TABLE_SIZE				37
#define CCK_TABLE_SIZE				33

#define BW_AUTO_SWITCH_HIGH_LOW			25
#define BW_AUTO_SWITCH_LOW_HIGH			30

#define DM_DIG_THRESH_HIGH			40
#define DM_DIG_THRESH_LOW			35

#define DM_FALSEALARM_THRESH_LOW		400
#define DM_FALSEALARM_THRESH_HIGH		1000

#define DM_DIG_MAX				0x3e
#define DM_DIG_MIN				0x1e

#define DM_DIG_MAX_AP				0x32
#define DM_DIG_MIN_AP				0x20

#define DM_DIG_FA_UPPER				0x3e
#define DM_DIG_FA_LOWER				0x1e
#define DM_DIG_FA_TH0				0x200
#define DM_DIG_FA_TH1				0x300
#define DM_DIG_FA_TH2				0x400

#define DM_DIG_BACKOFF_MAX			12
#define DM_DIG_BACKOFF_MIN			-4
#define DM_DIG_BACKOFF_DEFAULT			10

#define RXPATHSELECTION_DIFF_TH			18

#define DM_RATR_STA_INIT			0
#define DM_RATR_STA_HIGH			1
#define DM_RATR_STA_MIDDLE			2
#define DM_RATR_STA_LOW				3

#define CTS2SELF_THVAL				30
#define REGC38_TH				20

#define TXHIGHPWRLEVEL_NORMAL			0
#define TXHIGHPWRLEVEL_LEVEL1			1
#define TXHIGHPWRLEVEL_LEVEL2			2
#define TXHIGHPWRLEVEL_BT1			3
#define TXHIGHPWRLEVEL_BT2			4

#define DM_TYPE_BYFW				0
#define DM_TYPE_BYDRIVER			1

#define TX_POWER_NEAR_FIELD_THRESH_LVL2		74
#define TX_POWER_NEAR_FIELD_THRESH_LVL1		67
#define TXPWRTRACK_MAX_IDX			6

/* Dynamic ATC switch */
#define ATC_STATUS_OFF				0x0 /* enable */
#define	ATC_STATUS_ON				0x1 /* disable */
#define	CFO_THRESHOLD_XTAL			10 /* kHz */
#define	CFO_THRESHOLD_ATC			80 /* kHz */

enum FAT_STATE {
	FAT_NORMAL_STATE	= 0,
	FAT_TRAINING_STATE	= 1,
};

enum tag_dynamic_init_gain_operation_type_definition {
	DIG_TYPE_THRESH_HIGH	= 0,
	DIG_TYPE_THRESH_LOW	= 1,
	DIG_TYPE_BACKOFF	= 2,
	DIG_TYPE_RX_GAIN_MIN	= 3,
	DIG_TYPE_RX_GAIN_MAX	= 4,
	DIG_TYPE_ENABLE		= 5,
	DIG_TYPE_DISABLE	= 6,
	DIG_OP_TYPE_MAX
};

enum dm_1r_cca_e {
	CCA_1R		= 0,
	CCA_2R		= 1,
	CCA_MAX		= 2,
};

enum dm_rf_e {
	RF_SAVE		= 0,
	RF_NORMAL	= 1,
	RF_MAX		= 2,
};

enum dm_sw_ant_switch_e {
	ANS_ANTENNA_B	= 1,
	ANS_ANTENNA_A	= 2,
	ANS_ANTENNA_MAX	= 3,
};

enum dm_dig_ext_port_alg_e {
	DIG_EXT_PORT_STAGE_0	= 0,
	DIG_EXT_PORT_STAGE_1	= 1,
	DIG_EXT_PORT_STAGE_2	= 2,
	DIG_EXT_PORT_STAGE_3	= 3,
	DIG_EXT_PORT_STAGE_MAX	= 4,
};

enum dm_dig_connect_e {
	DIG_STA_DISCONNECT	= 0,
	DIG_STA_CONNECT		= 1,
	DIG_STA_BEFORE_CONNECT	= 2,
	DIG_MULTISTA_DISCONNECT	= 3,
	DIG_MULTISTA_CONNECT	= 4,
	DIG_CONNECT_MAX
};

enum pwr_track_control_method {
	BBSWING,
	TXAGC
};

#define BT_RSSI_STATE_NORMAL_POWER      BIT_OFFSET_LEN_MASK_32(0, 1)
#define BT_RSSI_STATE_AMDPU_OFF         BIT_OFFSET_LEN_MASK_32(1, 1)
#define BT_RSSI_STATE_SPECIAL_LOW       BIT_OFFSET_LEN_MASK_32(2, 1)
#define BT_RSSI_STATE_BG_EDCA_LOW       BIT_OFFSET_LEN_MASK_32(3, 1)
#define BT_RSSI_STATE_TXPOWER_LOW       BIT_OFFSET_LEN_MASK_32(4, 1)

void rtl8723be_dm_set_tx_ant_by_tx_info(struct ieee80211_hw *hw, u8 *pdesc,
					u32 mac_id);
void rtl8723be_dm_ant_sel_statistics(struct ieee80211_hw *hw, u8 antsel_tr_mux,
				     u32 mac_id, u32 rx_pwdb_all);
void rtl8723be_dm_fast_antenna_trainning_callback(unsigned long data);
void rtl8723be_dm_init(struct ieee80211_hw *hw);
void rtl8723be_dm_watchdog(struct ieee80211_hw *hw);
void rtl8723be_dm_write_dig(struct ieee80211_hw *hw, u8 current_igi);
void rtl8723be_dm_check_txpower_tracking(struct ieee80211_hw *hw);
void rtl8723be_dm_init_rate_adaptive_mask(struct ieee80211_hw *hw);
void rtl8723be_dm_txpower_track_adjust(struct ieee80211_hw *hw, u8 type,
				       u8 *pdirection, u32 *poutwrite_val);
void rtl8723be_dm_init_edca_turbo(struct ieee80211_hw *hw);

#endif
