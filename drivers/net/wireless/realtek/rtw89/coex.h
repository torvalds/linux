/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#ifndef __RTW89_COEX_H__
#define __RTW89_COEX_H__

#include "core.h"

enum btc_mode {
	BTC_MODE_NORMAL,
	BTC_MODE_WL,
	BTC_MODE_BT,
	BTC_MODE_WLOFF,
	BTC_MODE_MAX
};

enum btc_wl_rfk_type {
	BTC_WRFKT_IQK = 0,
	BTC_WRFKT_LCK = 1,
	BTC_WRFKT_DPK = 2,
	BTC_WRFKT_TXGAPK = 3,
	BTC_WRFKT_DACK = 4,
	BTC_WRFKT_RXDCK = 5,
	BTC_WRFKT_TSSI = 6,
};

#define NM_EXEC false
#define FC_EXEC true

#define RTW89_COEX_ACT1_WORK_PERIOD	round_jiffies_relative(HZ * 4)
#define RTW89_COEX_BT_DEVINFO_WORK_PERIOD	round_jiffies_relative(HZ * 16)
#define RTW89_COEX_RFK_CHK_WORK_PERIOD	msecs_to_jiffies(300)
#define BTC_RFK_PATH_MAP GENMASK(3, 0)
#define BTC_RFK_PHY_MAP GENMASK(5, 4)
#define BTC_RFK_BAND_MAP GENMASK(7, 6)

enum btc_wl_rfk_state {
	BTC_WRFK_STOP = 0,
	BTC_WRFK_START = 1,
	BTC_WRFK_ONESHOT_START = 2,
	BTC_WRFK_ONESHOT_STOP = 3,
};

enum btc_pri {
	BTC_PRI_MASK_RX_RESP = 0,
	BTC_PRI_MASK_TX_RESP,
	BTC_PRI_MASK_BEACON,
	BTC_PRI_MASK_RX_CCK,
	BTC_PRI_MASK_TX_MNGQ,
	BTC_PRI_MASK_MAX,
};

enum btc_bt_trs {
	BTC_BT_SS_GROUP = 0x0,
	BTC_BT_TX_GROUP = 0x2,
	BTC_BT_RX_GROUP = 0x3,
	BTC_BT_MAX_GROUP,
};

enum btc_rssi_st {
	BTC_RSSI_ST_LOW = 0x0,
	BTC_RSSI_ST_HIGH,
	BTC_RSSI_ST_STAY_LOW,
	BTC_RSSI_ST_STAY_HIGH,
	BTC_RSSI_ST_MAX
};

#define	BTC_RSSI_HIGH(_rssi_) \
	({typeof(_rssi_) __rssi = (_rssi_); \
	  ((__rssi == BTC_RSSI_ST_HIGH || \
	    __rssi == BTC_RSSI_ST_STAY_HIGH) ? 1 : 0); })

#define	BTC_RSSI_LOW(_rssi_) \
	({typeof(_rssi_) __rssi = (_rssi_); \
	  ((__rssi == BTC_RSSI_ST_LOW || \
	    __rssi == BTC_RSSI_ST_STAY_LOW) ? 1 : 0); })

#define BTC_RSSI_CHANGE(_rssi_) \
	({typeof(_rssi_) __rssi = (_rssi_); \
	  ((__rssi == BTC_RSSI_ST_LOW || \
	    __rssi == BTC_RSSI_ST_HIGH) ? 1 : 0); })

enum btc_ant {
	BTC_ANT_SHARED = 0,
	BTC_ANT_DEDICATED,
	BTC_ANTTYPE_MAX
};

enum btc_bt_btg {
	BTC_BT_ALONE = 0,
	BTC_BT_BTG
};

enum btc_switch {
	BTC_SWITCH_INTERNAL = 0,
	BTC_SWITCH_EXTERNAL
};

enum btc_pkt_type {
	PACKET_DHCP,
	PACKET_ARP,
	PACKET_EAPOL,
	PACKET_EAPOL_END,
	PACKET_ICMP,
	PACKET_MAX
};

enum btc_bt_mailbox_id {
	BTC_BTINFO_REPLY = 0x23,
	BTC_BTINFO_AUTO = 0x27
};

enum btc_role_state {
	BTC_ROLE_START,
	BTC_ROLE_STOP,
	BTC_ROLE_CHG_TYPE,
	BTC_ROLE_MSTS_STA_CONN_START,
	BTC_ROLE_MSTS_STA_CONN_END,
	BTC_ROLE_MSTS_STA_DIS_CONN,
	BTC_ROLE_MSTS_AP_START,
	BTC_ROLE_MSTS_AP_STOP,
	BTC_ROLE_STATE_UNKNOWN
};

enum btc_rfctrl {
	BTC_RFCTRL_WL_OFF,
	BTC_RFCTRL_WL_ON,
	BTC_RFCTRL_FW_CTRL,
	BTC_RFCTRL_MAX
};

enum btc_lps_state {
	BTC_LPS_OFF = 0,
	BTC_LPS_RF_OFF = 1,
	BTC_LPS_RF_ON = 2
};

void rtw89_btc_ntfy_poweron(struct rtw89_dev *rtwdev);
void rtw89_btc_ntfy_poweroff(struct rtw89_dev *rtwdev);
void rtw89_btc_ntfy_init(struct rtw89_dev *rtwdev, u8 mode);
void rtw89_btc_ntfy_scan_start(struct rtw89_dev *rtwdev, u8 phy_idx, u8 band);
void rtw89_btc_ntfy_scan_finish(struct rtw89_dev *rtwdev, u8 phy_idx);
void rtw89_btc_ntfy_switch_band(struct rtw89_dev *rtwdev, u8 phy_idx, u8 band);
void rtw89_btc_ntfy_specific_packet(struct rtw89_dev *rtwdev,
				    enum btc_pkt_type pkt_type);
void rtw89_btc_ntfy_eapol_packet_work(struct work_struct *work);
void rtw89_btc_ntfy_arp_packet_work(struct work_struct *work);
void rtw89_btc_ntfy_dhcp_packet_work(struct work_struct *work);
void rtw89_btc_ntfy_icmp_packet_work(struct work_struct *work);
void rtw89_btc_ntfy_role_info(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
			      struct rtw89_sta *rtwsta, enum btc_role_state state);
void rtw89_btc_ntfy_radio_state(struct rtw89_dev *rtwdev, enum btc_rfctrl rf_state);
void rtw89_btc_ntfy_wl_rfk(struct rtw89_dev *rtwdev, u8 phy_map,
			   enum btc_wl_rfk_type type,
			   enum btc_wl_rfk_state state);
void rtw89_btc_ntfy_wl_sta(struct rtw89_dev *rtwdev);
void rtw89_btc_c2h_handle(struct rtw89_dev *rtwdev, struct sk_buff *skb,
			  u32 len, u8 class, u8 func);
void rtw89_btc_dump_info(struct rtw89_dev *rtwdev, struct seq_file *m);
void rtw89_coex_act1_work(struct work_struct *work);
void rtw89_coex_bt_devinfo_work(struct work_struct *work);
void rtw89_coex_rfk_chk_work(struct work_struct *work);
void rtw89_coex_power_on(struct rtw89_dev *rtwdev);
void rtw89_btc_set_policy(struct rtw89_dev *rtwdev, u16 policy_type);
void rtw89_btc_set_policy_v1(struct rtw89_dev *rtwdev, u16 policy_type);

static inline u8 rtw89_btc_phymap(struct rtw89_dev *rtwdev,
				  enum rtw89_phy_idx phy_idx,
				  enum rtw89_rf_path_bit paths)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	u8 phy_map;

	phy_map = FIELD_PREP(BTC_RFK_PATH_MAP, paths) |
		  FIELD_PREP(BTC_RFK_PHY_MAP, BIT(phy_idx)) |
		  FIELD_PREP(BTC_RFK_BAND_MAP, chan->band_type);

	return phy_map;
}

static inline u8 rtw89_btc_path_phymap(struct rtw89_dev *rtwdev,
				       enum rtw89_phy_idx phy_idx,
				       enum rtw89_rf_path path)
{
	return rtw89_btc_phymap(rtwdev, phy_idx, BIT(path));
}

#endif
