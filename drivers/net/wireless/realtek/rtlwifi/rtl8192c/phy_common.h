/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL92C_PHY_COMMON_H__
#define __RTL92C_PHY_COMMON_H__

#define MAX_PRECMD_CNT			16
#define MAX_RFDEPENDCMD_CNT		16
#define MAX_POSTCMD_CNT			16

#define MAX_DOZE_WAITING_TIMES_9x	64

#define RT_CANNOT_IO(hw)		false
#define HIGHPOWER_RADIOA_ARRAYLEN	22

#define MAX_TOLERANCE			5

#define	APK_BB_REG_NUM			5
#define	APK_AFE_REG_NUM			16
#define	APK_CURVE_REG_NUM		4
#define	PATH_NUM			2

#define LOOP_LIMIT			5
#define MAX_STALL_TIME			50
#define ANTENNADIVERSITYVALUE		0x80
#define MAX_TXPWR_IDX_NMODE_92S		63
#define RESET_CNT_LIMIT			3

#define IQK_ADDA_REG_NUM		16
#define IQK_MAC_REG_NUM			4

#define IQK_DELAY_TIME			1
#define RF90_PATH_MAX			2

#define CT_OFFSET_MAC_ADDR		0X16

#define CT_OFFSET_CCK_TX_PWR_IDX	0x5A
#define CT_OFFSET_HT401S_TX_PWR_IDX	0x60
#define CT_OFFSET_HT402S_TX_PWR_IDX_DIF	0x66
#define CT_OFFSET_HT20_TX_PWR_IDX_DIFF	0x69
#define CT_OFFSET_OFDM_TX_PWR_IDX_DIFF	0x6C

#define CT_OFFSET_HT40_MAX_PWR_OFFSET	0x6F
#define CT_OFFSET_HT20_MAX_PWR_OFFSET	0x72

#define CT_OFFSET_CHANNEL_PLAH		0x75
#define CT_OFFSET_THERMAL_METER		0x78
#define CT_OFFSET_RF_OPTION		0x79
#define CT_OFFSET_VERSION		0x7E
#define CT_OFFSET_CUSTOMER_ID		0x7F

#define RTL92C_MAX_PATH_NUM		2
#define LLT_LAST_ENTRY_OF_TX_PKT_BUFFER	255

enum swchnlcmd_id {
	CMDID_END,
	CMDID_SET_TXPOWEROWER_LEVEL,
	CMDID_BBREGWRITE10,
	CMDID_WRITEPORT_ULONG,
	CMDID_WRITEPORT_USHORT,
	CMDID_WRITEPORT_UCHAR,
	CMDID_RF_WRITEREG,
};

struct swchnlcmd {
	enum swchnlcmd_id cmdid;
	u32 para1;
	u32 para2;
	u32 msdelay;
};

enum hw90_block_e {
	HW90_BLOCK_MAC = 0,
	HW90_BLOCK_PHY0 = 1,
	HW90_BLOCK_PHY1 = 2,
	HW90_BLOCK_RF = 3,
	HW90_BLOCK_MAXIMUM = 4,
};

enum baseband_config_type {
	BASEBAND_CONFIG_PHY_REG = 0,
	BASEBAND_CONFIG_AGC_TAB = 1,
};

enum ra_offset_area {
	RA_OFFSET_LEGACY_OFDM1,
	RA_OFFSET_LEGACY_OFDM2,
	RA_OFFSET_HT_OFDM1,
	RA_OFFSET_HT_OFDM2,
	RA_OFFSET_HT_OFDM3,
	RA_OFFSET_HT_OFDM4,
	RA_OFFSET_HT_CCK,
};

enum antenna_path {
	ANTENNA_NONE,
	ANTENNA_D,
	ANTENNA_C,
	ANTENNA_CD,
	ANTENNA_B,
	ANTENNA_BD,
	ANTENNA_BC,
	ANTENNA_BCD,
	ANTENNA_A,
	ANTENNA_AD,
	ANTENNA_AC,
	ANTENNA_ACD,
	ANTENNA_AB,
	ANTENNA_ABD,
	ANTENNA_ABC,
	ANTENNA_ABCD
};

struct r_antenna_select_ofdm {
	u32 r_tx_antenna:4;
	u32 r_ant_l:4;
	u32 r_ant_non_ht:4;
	u32 r_ant_ht1:4;
	u32 r_ant_ht2:4;
	u32 r_ant_ht_s1:4;
	u32 r_ant_non_ht_s1:4;
	u32 ofdm_txsc:2;
	u32 reserved:2;
};

struct r_antenna_select_cck {
	u8 r_cckrx_enable_2:2;
	u8 r_cckrx_enable:2;
	u8 r_ccktx_enable:4;
};

struct efuse_contents {
	u8 mac_addr[ETH_ALEN];
	u8 cck_tx_power_idx[6];
	u8 ht40_1s_tx_power_idx[6];
	u8 ht40_2s_tx_power_idx_diff[3];
	u8 ht20_tx_power_idx_diff[3];
	u8 ofdm_tx_power_idx_diff[3];
	u8 ht40_max_power_offset[3];
	u8 ht20_max_power_offset[3];
	u8 channel_plan;
	u8 thermal_meter;
	u8 rf_option[5];
	u8 version;
	u8 oem_id;
	u8 regulatory;
};

struct tx_power_struct {
	u8 cck[RTL92C_MAX_PATH_NUM][CHANNEL_MAX_NUMBER];
	u8 ht40_1s[RTL92C_MAX_PATH_NUM][CHANNEL_MAX_NUMBER];
	u8 ht40_2s[RTL92C_MAX_PATH_NUM][CHANNEL_MAX_NUMBER];
	u8 ht20_diff[RTL92C_MAX_PATH_NUM][CHANNEL_MAX_NUMBER];
	u8 legacy_ht_diff[RTL92C_MAX_PATH_NUM][CHANNEL_MAX_NUMBER];
	u8 legacy_ht_txpowerdiff;
	u8 groupht20[RTL92C_MAX_PATH_NUM][CHANNEL_MAX_NUMBER];
	u8 groupht40[RTL92C_MAX_PATH_NUM][CHANNEL_MAX_NUMBER];
	u8 pwrgroup_cnt;
	u32 mcs_original_offset[4][16];
};

u32 rtl92c_phy_query_bb_reg(struct ieee80211_hw *hw,
				   u32 regaddr, u32 bitmask);
void rtl92c_phy_set_bb_reg(struct ieee80211_hw *hw,
				  u32 regaddr, u32 bitmask, u32 data);
u32 rtl92c_phy_query_rf_reg(struct ieee80211_hw *hw,
				   enum radio_path rfpath, u32 regaddr,
				   u32 bitmask);
bool rtl92c_phy_mac_config(struct ieee80211_hw *hw);
bool rtl92c_phy_bb_config(struct ieee80211_hw *hw);
bool rtl92c_phy_rf_config(struct ieee80211_hw *hw);
bool rtl92c_phy_config_rf_with_feaderfile(struct ieee80211_hw *hw,
						 enum radio_path rfpath);
void rtl92c_phy_get_hw_reg_originalvalue(struct ieee80211_hw *hw);
void rtl92c_phy_get_txpower_level(struct ieee80211_hw *hw,
					 long *powerlevel);
void rtl92c_phy_set_txpower_level(struct ieee80211_hw *hw, u8 channel);
bool rtl92c_phy_update_txpower_dbm(struct ieee80211_hw *hw,
					  long power_indbm);
void rtl92c_phy_set_bw_mode(struct ieee80211_hw *hw,
				   enum nl80211_channel_type ch_type);
void rtl92c_phy_sw_chnl_callback(struct ieee80211_hw *hw);
u8 rtl92c_phy_sw_chnl(struct ieee80211_hw *hw);
void rtl92c_phy_iq_calibrate(struct ieee80211_hw *hw, bool b_recovery);
void rtl92c_phy_set_beacon_hw_reg(struct ieee80211_hw *hw,
					 u16 beaconinterval);
void rtl92c_phy_ap_calibrate(struct ieee80211_hw *hw, s8 delta);
void rtl92c_phy_lc_calibrate(struct ieee80211_hw *hw);
void rtl92c_phy_set_rfpath_switch(struct ieee80211_hw *hw, bool bmain);
bool rtl92c_phy_config_rf_with_headerfile(struct ieee80211_hw *hw,
					  enum radio_path rfpath);
bool rtl8192_phy_check_is_legal_rfpath(struct ieee80211_hw *hw,
					      u32 rfpath);
bool rtl92c_phy_set_rf_power_state(struct ieee80211_hw *hw,
					  enum rf_pwrstate rfpwr_state);
void rtl92ce_phy_set_rf_on(struct ieee80211_hw *hw);
void rtl92c_phy_set_io(struct ieee80211_hw *hw);
void rtl92c_bb_block_on(struct ieee80211_hw *hw);
u32 _rtl92c_phy_calculate_bit_shift(u32 bitmask);
long _rtl92c_phy_txpwr_idx_to_dbm(struct ieee80211_hw *hw,
				  enum wireless_mode wirelessmode,
				  u8 txpwridx);
u8 _rtl92c_phy_dbm_to_txpwr_idx(struct ieee80211_hw *hw,
				enum wireless_mode wirelessmode,
				long power_indbm);
void _rtl92c_phy_init_bb_rf_register_definition(struct ieee80211_hw *hw);
void _rtl92c_phy_set_rf_sleep(struct ieee80211_hw *hw);
bool _rtl92c_phy_sw_chnl_step_by_step(struct ieee80211_hw *hw,
				      u8 channel, u8 *stage, u8 *step,
				      u32 *delay);
u8 rtl92c_bt_rssi_state_change(struct ieee80211_hw *hw);
u32 _rtl92c_phy_fw_rf_serial_read(struct ieee80211_hw *hw,
				  enum radio_path rfpath, u32 offset);
void _rtl92c_phy_fw_rf_serial_write(struct ieee80211_hw *hw,
				    enum radio_path rfpath, u32 offset,
				    u32 data);
u32 _rtl92c_phy_rf_serial_read(struct ieee80211_hw *hw,
			       enum radio_path rfpath, u32 offset);
void _rtl92c_phy_rf_serial_write(struct ieee80211_hw *hw,
				 enum radio_path rfpath, u32 offset,
				 u32 data);
bool _rtl92c_phy_bb8192c_config_parafile(struct ieee80211_hw *hw);
void _rtl92c_store_pwrindex_diffrate_offset(struct ieee80211_hw *hw,
					    u32 regaddr, u32 bitmask,
					    u32 data);
bool rtl92c_phy_set_io_cmd(struct ieee80211_hw *hw, enum io_type iotype);

#endif
