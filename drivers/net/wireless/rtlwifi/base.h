/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
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
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __RTL_BASE_H__
#define __RTL_BASE_H__

enum ap_peer {
	PEER_UNKNOWN = 0,
	PEER_RTL = 1,
	PEER_RTL_92SE = 2,
	PEER_BROAD = 3,
	PEER_RAL = 4,
	PEER_ATH = 5,
	PEER_CISCO = 6,
	PEER_MARV = 7,
	PEER_AIRGO = 9,
	PEER_MAX = 10,
};

#define RTL_DUMMY_OFFSET	0
#define RTL_DUMMY_UNIT		8
#define RTL_TX_DUMMY_SIZE	(RTL_DUMMY_OFFSET * RTL_DUMMY_UNIT)
#define RTL_TX_DESC_SIZE	32
#define RTL_TX_HEADER_SIZE	(RTL_TX_DESC_SIZE + RTL_TX_DUMMY_SIZE)

#define MAX_BIT_RATE_40MHZ_MCS15	300	/* Mbps */
#define MAX_BIT_RATE_40MHZ_MCS7		150	/* Mbps */

#define MAX_BIT_RATE_SHORT_GI_2NSS_80MHZ_MCS9	867	/* Mbps */
#define MAX_BIT_RATE_SHORT_GI_2NSS_80MHZ_MCS7	650	/* Mbps */
#define MAX_BIT_RATE_LONG_GI_2NSS_80MHZ_MCS9	780	/* Mbps */
#define MAX_BIT_RATE_LONG_GI_2NSS_80MHZ_MCS7	585	/* Mbps */

#define MAX_BIT_RATE_SHORT_GI_1NSS_80MHZ_MCS9	434	/* Mbps */
#define MAX_BIT_RATE_SHORT_GI_1NSS_80MHZ_MCS7	325	/* Mbps */
#define MAX_BIT_RATE_LONG_GI_1NSS_80MHZ_MCS9	390	/* Mbps */
#define MAX_BIT_RATE_LONG_GI_1NSS_80MHZ_MCS7	293	/* Mbps */

#define FRAME_OFFSET_FRAME_CONTROL	0
#define FRAME_OFFSET_DURATION		2
#define FRAME_OFFSET_ADDRESS1		4
#define FRAME_OFFSET_ADDRESS2		10
#define FRAME_OFFSET_ADDRESS3		16
#define FRAME_OFFSET_SEQUENCE		22
#define FRAME_OFFSET_ADDRESS4		24

#define SET_80211_HDR_FRAME_CONTROL(_hdr, _val)		\
	WRITEEF2BYTE(_hdr, _val)
#define SET_80211_HDR_TYPE_AND_SUBTYPE(_hdr, _val)	\
	WRITEEF1BYTE(_hdr, _val)
#define SET_80211_HDR_PWR_MGNT(_hdr, _val)		\
	SET_BITS_TO_LE_2BYTE(_hdr, 12, 1, _val)
#define SET_80211_HDR_TO_DS(_hdr, _val)			\
	SET_BITS_TO_LE_2BYTE(_hdr, 8, 1, _val)

#define SET_80211_PS_POLL_AID(_hdr, _val)		\
	(*(u16 *)((u8 *)(_hdr) + 2) = _val)
#define SET_80211_PS_POLL_BSSID(_hdr, _val)		\
	ether_addr_copy(((u8 *)(_hdr)) + 4, (u8 *)(_val))
#define SET_80211_PS_POLL_TA(_hdr, _val)		\
	ether_addr_copy(((u8 *)(_hdr))+10, (u8 *)(_val))

#define SET_80211_HDR_DURATION(_hdr, _val)	\
	(*(u16 *)((u8 *)(_hdr) + FRAME_OFFSET_DURATION) = le16_to_cpu(_val))
#define SET_80211_HDR_ADDRESS1(_hdr, _val)	\
	CP_MACADDR((u8 *)(_hdr)+FRAME_OFFSET_ADDRESS1, (u8 *)(_val))
#define SET_80211_HDR_ADDRESS2(_hdr, _val)	\
	CP_MACADDR((u8 *)(_hdr)+FRAME_OFFSET_ADDRESS2, (u8 *)(_val))
#define SET_80211_HDR_ADDRESS3(_hdr, _val)	\
	CP_MACADDR((u8 *)(_hdr)+FRAME_OFFSET_ADDRESS3, (u8 *)(_val))
#define SET_80211_HDR_FRAGMENT_SEQUENCE(_hdr, _val)  \
	WRITEEF2BYTE((u8 *)(_hdr)+FRAME_OFFSET_SEQUENCE, _val)

#define SET_BEACON_PROBE_RSP_TIME_STAMP_LOW(__phdr, __val)	\
	WRITEEF4BYTE(((u8 *)(__phdr)) + 24, __val)
#define SET_BEACON_PROBE_RSP_TIME_STAMP_HIGH(__phdr, __val) \
	WRITEEF4BYTE(((u8 *)(__phdr)) + 28, __val)
#define SET_BEACON_PROBE_RSP_BEACON_INTERVAL(__phdr, __val) \
	WRITEEF2BYTE(((u8 *)(__phdr)) + 32, __val)
#define GET_BEACON_PROBE_RSP_CAPABILITY_INFO(__phdr)		\
	READEF2BYTE(((u8 *)(__phdr)) + 34)
#define SET_BEACON_PROBE_RSP_CAPABILITY_INFO(__phdr, __val) \
	WRITEEF2BYTE(((u8 *)(__phdr)) + 34, __val)
#define MASK_BEACON_PROBE_RSP_CAPABILITY_INFO(__phdr, __val) \
	SET_BEACON_PROBE_RSP_CAPABILITY_INFO(__phdr, \
	(GET_BEACON_PROBE_RSP_CAPABILITY_INFO(__phdr) & (~(__val))))

int rtl_init_core(struct ieee80211_hw *hw);
void rtl_deinit_core(struct ieee80211_hw *hw);
void rtl_init_rx_config(struct ieee80211_hw *hw);
void rtl_init_rfkill(struct ieee80211_hw *hw);
void rtl_deinit_rfkill(struct ieee80211_hw *hw);

void rtl_watch_dog_timer_callback(unsigned long data);
void rtl_deinit_deferred_work(struct ieee80211_hw *hw);

bool rtl_action_proc(struct ieee80211_hw *hw, struct sk_buff *skb, u8 is_tx);
int rtlwifi_rate_mapping(struct ieee80211_hw *hw, bool isht,
			 bool isvht, u8 desc_rate);
bool rtl_tx_mgmt_proc(struct ieee80211_hw *hw, struct sk_buff *skb);
u8 rtl_is_special_data(struct ieee80211_hw *hw, struct sk_buff *skb, u8 is_tx);

void rtl_beacon_statistic(struct ieee80211_hw *hw, struct sk_buff *skb);
void rtl_watch_dog_timer_callback(unsigned long data);
int rtl_tx_agg_start(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	struct ieee80211_sta *sta, u16 tid, u16 *ssn);
int rtl_tx_agg_stop(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	struct ieee80211_sta *sta, u16 tid);
int rtl_tx_agg_oper(struct ieee80211_hw *hw,
		    struct ieee80211_sta *sta, u16 tid);
int rtl_rx_agg_start(struct ieee80211_hw *hw,
		     struct ieee80211_sta *sta, u16 tid);
int rtl_rx_agg_stop(struct ieee80211_hw *hw,
		    struct ieee80211_sta *sta, u16 tid);
void rtl_watchdog_wq_callback(void *data);
void rtl_fwevt_wq_callback(void *data);

void rtl_get_tcb_desc(struct ieee80211_hw *hw,
		      struct ieee80211_tx_info *info,
		      struct ieee80211_sta *sta,
		      struct sk_buff *skb, struct rtl_tcb_desc *tcb_desc);

int rtl_send_smps_action(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta,
		enum ieee80211_smps_mode smps);
u8 *rtl_find_ie(u8 *data, unsigned int len, u8 ie);
void rtl_recognize_peer(struct ieee80211_hw *hw, u8 *data, unsigned int len);
u8 rtl_tid_to_ac(u8 tid);
extern struct attribute_group rtl_attribute_group;
void rtl_easy_concurrent_retrytimer_callback(unsigned long data);
extern struct rtl_global_var rtl_global_var;
void rtl_phy_scan_operation_backup(struct ieee80211_hw *hw, u8 operation);

#endif
