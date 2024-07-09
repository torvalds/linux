/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2024  Realtek Corporation
 */

#ifndef __RTW89_8852BX_H__
#define __RTW89_8852BX_H__

#include "core.h"

#define RF_PATH_NUM_8852BX 2
#define BB_PATH_NUM_8852BX 2

enum rtw8852bx_pmac_mode {
	NONE_TEST,
	PKTS_TX,
	PKTS_RX,
	CONT_TX
};

struct rtw8852bx_u_efuse {
	u8 rsvd[0x88];
	u8 mac_addr[ETH_ALEN];
};

struct rtw8852bx_e_efuse {
	u8 mac_addr[ETH_ALEN];
};

struct rtw8852bx_tssi_offset {
	u8 cck_tssi[TSSI_CCK_CH_GROUP_NUM];
	u8 bw40_tssi[TSSI_MCS_2G_CH_GROUP_NUM];
	u8 rsvd[7];
	u8 bw40_1s_tssi_5g[TSSI_MCS_5G_CH_GROUP_NUM];
} __packed;

struct rtw8852bx_efuse {
	u8 rsvd[0x210];
	struct rtw8852bx_tssi_offset path_a_tssi;
	u8 rsvd1[10];
	struct rtw8852bx_tssi_offset path_b_tssi;
	u8 rsvd2[94];
	u8 channel_plan;
	u8 xtal_k;
	u8 rsvd3;
	u8 iqk_lck;
	u8 rsvd4[5];
	u8 reg_setting:2;
	u8 tx_diversity:1;
	u8 rx_diversity:2;
	u8 ac_mode:1;
	u8 module_type:2;
	u8 rsvd5;
	u8 shared_ant:1;
	u8 coex_type:3;
	u8 ant_iso:1;
	u8 radio_on_off:1;
	u8 rsvd6:2;
	u8 eeprom_version;
	u8 customer_id;
	u8 tx_bb_swing_2g;
	u8 tx_bb_swing_5g;
	u8 tx_cali_pwr_trk_mode;
	u8 trx_path_selection;
	u8 rfe_type;
	u8 country_code[2];
	u8 rsvd7[3];
	u8 path_a_therm;
	u8 path_b_therm;
	u8 rsvd8[2];
	u8 rx_gain_2g_ofdm;
	u8 rsvd9;
	u8 rx_gain_2g_cck;
	u8 rsvd10;
	u8 rx_gain_5g_low;
	u8 rsvd11;
	u8 rx_gain_5g_mid;
	u8 rsvd12;
	u8 rx_gain_5g_high;
	u8 rsvd13[35];
	u8 path_a_cck_pwr_idx[6];
	u8 path_a_bw40_1tx_pwr_idx[5];
	u8 path_a_ofdm_1tx_pwr_idx_diff:4;
	u8 path_a_bw20_1tx_pwr_idx_diff:4;
	u8 path_a_bw20_2tx_pwr_idx_diff:4;
	u8 path_a_bw40_2tx_pwr_idx_diff:4;
	u8 path_a_cck_2tx_pwr_idx_diff:4;
	u8 path_a_ofdm_2tx_pwr_idx_diff:4;
	u8 rsvd14[0xf2];
	union {
		struct rtw8852bx_u_efuse u;
		struct rtw8852bx_e_efuse e;
	};
} __packed;

struct rtw8852bx_bb_pmac_info {
	u8 en_pmac_tx:1;
	u8 is_cck:1;
	u8 mode:3;
	u8 rsvd:3;
	u16 tx_cnt;
	u16 period;
	u16 tx_time;
	u8 duty_cycle;
};

struct rtw8852bx_bb_tssi_bak {
	u8 tx_path;
	u8 rx_path;
	u32 p0_rfmode;
	u32 p0_rfmode_ftm;
	u32 p1_rfmode;
	u32 p1_rfmode_ftm;
	s16 tx_pwr; /* S9 */
};

struct rtw8852bx_info {
	int (*mac_enable_bb_rf)(struct rtw89_dev *rtwdev);
	int (*mac_disable_bb_rf)(struct rtw89_dev *rtwdev);
	void (*bb_sethw)(struct rtw89_dev *rtwdev);
	void (*bb_reset_all)(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx);
	void (*bb_cfg_txrx_path)(struct rtw89_dev *rtwdev);
	void (*bb_cfg_tx_path)(struct rtw89_dev *rtwdev, u8 tx_path);
	void (*bb_ctrl_rx_path)(struct rtw89_dev *rtwdev,
				enum rtw89_rf_path_bit rx_path);
	void (*bb_set_plcp_tx)(struct rtw89_dev *rtwdev);
	void (*bb_set_power)(struct rtw89_dev *rtwdev, s16 pwr_dbm,
			     enum rtw89_phy_idx idx);
	void (*bb_set_pmac_pkt_tx)(struct rtw89_dev *rtwdev, u8 enable,
				   u16 tx_cnt, u16 period, u16 tx_time,
				   enum rtw89_phy_idx idx);
	void (*bb_backup_tssi)(struct rtw89_dev *rtwdev, enum rtw89_phy_idx idx,
			       struct rtw8852bx_bb_tssi_bak *bak);
	void (*bb_restore_tssi)(struct rtw89_dev *rtwdev, enum rtw89_phy_idx idx,
				const struct rtw8852bx_bb_tssi_bak *bak);
	void (*bb_tx_mode_switch)(struct rtw89_dev *rtwdev,
				  enum rtw89_phy_idx idx, u8 mode);
	void (*set_channel_mac)(struct rtw89_dev *rtwdev,
				const struct rtw89_chan *chan, u8 mac_idx);
	void (*set_channel_bb)(struct rtw89_dev *rtwdev, const struct rtw89_chan *chan,
			       enum rtw89_phy_idx phy_idx);
	void (*ctrl_nbtg_bt_tx)(struct rtw89_dev *rtwdev, bool en,
				enum rtw89_phy_idx phy_idx);
	void (*ctrl_btg_bt_rx)(struct rtw89_dev *rtwdev, bool en,
			       enum rtw89_phy_idx phy_idx);
	void (*query_ppdu)(struct rtw89_dev *rtwdev,
			   struct rtw89_rx_phy_ppdu *phy_ppdu,
			   struct ieee80211_rx_status *status);
	int (*read_efuse)(struct rtw89_dev *rtwdev, u8 *log_map,
			  enum rtw89_efuse_block block);
	int (*read_phycap)(struct rtw89_dev *rtwdev, u8 *phycap_map);
	void (*power_trim)(struct rtw89_dev *rtwdev);
	void (*set_txpwr)(struct rtw89_dev *rtwdev,
			  const struct rtw89_chan *chan,
			  enum rtw89_phy_idx phy_idx);
	void (*set_txpwr_ctrl)(struct rtw89_dev *rtwdev,
			       enum rtw89_phy_idx phy_idx);
	int (*init_txpwr_unit)(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx);
	void (*set_txpwr_ul_tb_offset)(struct rtw89_dev *rtwdev,
				       s8 pw_ofst, enum rtw89_mac_idx mac_idx);
	u8 (*get_thermal)(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path);
	void (*adc_cfg)(struct rtw89_dev *rtwdev, u8 bw, u8 path);
	void (*btc_init_cfg)(struct rtw89_dev *rtwdev);
	void (*btc_set_wl_pri)(struct rtw89_dev *rtwdev, u8 map, bool state);
	s8 (*btc_get_bt_rssi)(struct rtw89_dev *rtwdev, s8 val);
	void (*btc_update_bt_cnt)(struct rtw89_dev *rtwdev);
	void (*btc_wl_s1_standby)(struct rtw89_dev *rtwdev, bool state);
	void (*btc_set_wl_rx_gain)(struct rtw89_dev *rtwdev, u32 level);
};

extern const struct rtw8852bx_info rtw8852bx_info;

static inline
int rtw8852bx_mac_enable_bb_rf(struct rtw89_dev *rtwdev)
{
	return rtw8852bx_info.mac_enable_bb_rf(rtwdev);
}

static inline
int rtw8852bx_mac_disable_bb_rf(struct rtw89_dev *rtwdev)
{
	return rtw8852bx_info.mac_disable_bb_rf(rtwdev);
}

static inline
void rtw8852bx_bb_sethw(struct rtw89_dev *rtwdev)
{
	rtw8852bx_info.bb_sethw(rtwdev);
}

static inline
void rtw8852bx_bb_reset_all(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	rtw8852bx_info.bb_reset_all(rtwdev, phy_idx);
}

static inline
void rtw8852bx_bb_cfg_txrx_path(struct rtw89_dev *rtwdev)
{
	rtw8852bx_info.bb_cfg_txrx_path(rtwdev);
}

static inline
void rtw8852bx_bb_cfg_tx_path(struct rtw89_dev *rtwdev, u8 tx_path)
{
	rtw8852bx_info.bb_cfg_tx_path(rtwdev, tx_path);
}

static inline
void rtw8852bx_bb_ctrl_rx_path(struct rtw89_dev *rtwdev,
			       enum rtw89_rf_path_bit rx_path)
{
	rtw8852bx_info.bb_ctrl_rx_path(rtwdev, rx_path);
}

static inline
void rtw8852bx_bb_set_plcp_tx(struct rtw89_dev *rtwdev)
{
	rtw8852bx_info.bb_set_plcp_tx(rtwdev);
}

static inline
void rtw8852bx_bb_set_power(struct rtw89_dev *rtwdev, s16 pwr_dbm,
			    enum rtw89_phy_idx idx)
{
	rtw8852bx_info.bb_set_power(rtwdev, pwr_dbm, idx);
}

static inline
void rtw8852bx_bb_set_pmac_pkt_tx(struct rtw89_dev *rtwdev, u8 enable,
				  u16 tx_cnt, u16 period, u16 tx_time,
				  enum rtw89_phy_idx idx)
{
	rtw8852bx_info.bb_set_pmac_pkt_tx(rtwdev, enable, tx_cnt, period, tx_time, idx);
}

static inline
void rtw8852bx_bb_backup_tssi(struct rtw89_dev *rtwdev, enum rtw89_phy_idx idx,
			      struct rtw8852bx_bb_tssi_bak *bak)
{
	rtw8852bx_info.bb_backup_tssi(rtwdev, idx, bak);
}

static inline
void rtw8852bx_bb_restore_tssi(struct rtw89_dev *rtwdev, enum rtw89_phy_idx idx,
			       const struct rtw8852bx_bb_tssi_bak *bak)
{
	rtw8852bx_info.bb_restore_tssi(rtwdev, idx, bak);
}

static inline
void rtw8852bx_bb_tx_mode_switch(struct rtw89_dev *rtwdev,
				 enum rtw89_phy_idx idx, u8 mode)
{
	rtw8852bx_info.bb_tx_mode_switch(rtwdev, idx, mode);
}

static inline
void rtw8852bx_set_channel_mac(struct rtw89_dev *rtwdev,
			       const struct rtw89_chan *chan, u8 mac_idx)
{
	rtw8852bx_info.set_channel_mac(rtwdev, chan, mac_idx);
}

static inline
void rtw8852bx_set_channel_bb(struct rtw89_dev *rtwdev, const struct rtw89_chan *chan,
			      enum rtw89_phy_idx phy_idx)
{
	rtw8852bx_info.set_channel_bb(rtwdev, chan, phy_idx);
}

static inline
void rtw8852bx_ctrl_nbtg_bt_tx(struct rtw89_dev *rtwdev, bool en,
			       enum rtw89_phy_idx phy_idx)
{
	rtw8852bx_info.ctrl_nbtg_bt_tx(rtwdev, en, phy_idx);
}

static inline
void rtw8852bx_ctrl_btg_bt_rx(struct rtw89_dev *rtwdev, bool en,
			      enum rtw89_phy_idx phy_idx)
{
	rtw8852bx_info.ctrl_btg_bt_rx(rtwdev, en, phy_idx);
}

static inline
void rtw8852bx_query_ppdu(struct rtw89_dev *rtwdev,
			  struct rtw89_rx_phy_ppdu *phy_ppdu,
			  struct ieee80211_rx_status *status)
{
	rtw8852bx_info.query_ppdu(rtwdev, phy_ppdu, status);
}

static inline
int rtw8852bx_read_efuse(struct rtw89_dev *rtwdev, u8 *log_map,
			 enum rtw89_efuse_block block)
{
	return rtw8852bx_info.read_efuse(rtwdev, log_map, block);
}

static inline
int rtw8852bx_read_phycap(struct rtw89_dev *rtwdev, u8 *phycap_map)
{
	return rtw8852bx_info.read_phycap(rtwdev, phycap_map);
}

static inline
void rtw8852bx_power_trim(struct rtw89_dev *rtwdev)
{
	rtw8852bx_info.power_trim(rtwdev);
}

static inline
void rtw8852bx_set_txpwr(struct rtw89_dev *rtwdev,
			 const struct rtw89_chan *chan,
			 enum rtw89_phy_idx phy_idx)
{
	rtw8852bx_info.set_txpwr(rtwdev, chan, phy_idx);
}

static inline
void rtw8852bx_set_txpwr_ctrl(struct rtw89_dev *rtwdev,
			      enum rtw89_phy_idx phy_idx)
{
	rtw8852bx_info.set_txpwr_ctrl(rtwdev, phy_idx);
}

static inline
int rtw8852bx_init_txpwr_unit(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	return rtw8852bx_info.init_txpwr_unit(rtwdev, phy_idx);
}

static inline
void rtw8852bx_set_txpwr_ul_tb_offset(struct rtw89_dev *rtwdev,
				      s8 pw_ofst, enum rtw89_mac_idx mac_idx)
{
	rtw8852bx_info.set_txpwr_ul_tb_offset(rtwdev, pw_ofst, mac_idx);
}

static inline
u8 rtw8852bx_get_thermal(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path)
{
	return rtw8852bx_info.get_thermal(rtwdev, rf_path);
}

static inline
void rtw8852bx_adc_cfg(struct rtw89_dev *rtwdev, u8 bw, u8 path)
{
	rtw8852bx_info.adc_cfg(rtwdev, bw, path);
}

static inline
void rtw8852bx_btc_init_cfg(struct rtw89_dev *rtwdev)
{
	rtw8852bx_info.btc_init_cfg(rtwdev);
}

static inline
void rtw8852bx_btc_set_wl_pri(struct rtw89_dev *rtwdev, u8 map, bool state)
{
	rtw8852bx_info.btc_set_wl_pri(rtwdev, map, state);
}

static inline
s8 rtw8852bx_btc_get_bt_rssi(struct rtw89_dev *rtwdev, s8 val)
{
	return rtw8852bx_info.btc_get_bt_rssi(rtwdev, val);
}

static inline
void rtw8852bx_btc_update_bt_cnt(struct rtw89_dev *rtwdev)
{
	rtw8852bx_info.btc_update_bt_cnt(rtwdev);
}

static inline
void rtw8852bx_btc_wl_s1_standby(struct rtw89_dev *rtwdev, bool state)
{
	rtw8852bx_info.btc_wl_s1_standby(rtwdev, state);
}

static inline
void rtw8852bx_btc_set_wl_rx_gain(struct rtw89_dev *rtwdev, u32 level)
{
	rtw8852bx_info.btc_set_wl_rx_gain(rtwdev, level);
}

#endif
